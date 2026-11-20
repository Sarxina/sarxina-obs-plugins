#include "chat-trigger.hpp"

#include "plain-text.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace sarxina {

namespace {

constexpr int kLongestChatter = 25;    // characters, as long as a Twitch name
constexpr int kLongestRedeemText = 30; // characters of what the viewer wrote, in the settings' list of rewards
constexpr size_t kLongestCommand = 25;
constexpr size_t kRewardsKept = 20;
constexpr size_t kChattersKept = 500;  // cooldowns for each chatter, before the finished ones are let go
constexpr double kListAgainMs = 5000;  // the rewards are asked for at most this often when the settings open
constexpr int kLongestPrompt = 200;    // characters, as long as Twitch allows

char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// "!plinko" or "!plinko and more", in any case. `rest` is what follows it.
bool isCommand(const char *text, const std::string &command, QString &rest)
{
	if (!text || text[0] != '!' || command.empty())
		return false;
	for (size_t i = 0; i < command.size(); i++)
		if (text[i + 1] == '\0' || lower(text[i + 1]) != command[i])
			return false;
	const char after = text[command.size() + 1];
	if (after != '\0' && after != ' ')
		return false;
	rest = QString::fromUtf8(text + command.size() + 1).trimmed();
	return true;
}

QString utf8(const char *text)
{
	return QString::fromUtf8(text ? text : "");
}

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

// The chatter a cooldown is kept for: their id, else their account name.
std::string chatterKey(const sarxina_chat_message &message)
{
	if (message.user_id && *message.user_id)
		return message.user_id;
	return message.user_name ? message.user_name : "";
}

QString query(const char *name, const std::string &value)
{
	return QString::fromLatin1(name) + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(value)));
}

// Twitch's own words for what went wrong, or the manager's when it never reached Twitch.
QString why(int status, const QJsonObject &answer)
{
	const QString message = answer.value(QStringLiteral("message")).toString();
	if (!message.isEmpty())
		return message;
	return status ? QStringLiteral("Twitch said %1").arg(status) : QStringLiteral("Twitch could not be reached");
}

std::string keyed(const std::string &key, const char *name)
{
	return key + name;
}

// The settings show the command field or the reward fields, whichever the trigger needs.
bool triggerPicked(void *data, obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const std::string &key = static_cast<ChatTrigger *>(data)->key;
	const bool reward = std::strcmp(obs_data_get_string(settings, keyed(key, "trigger").c_str()), "reward") == 0;
	obs_property_set_visible(obs_properties_get(properties, keyed(key, "command").c_str()), !reward);
	for (const char *field : {"reward", "reward_create", "reward_help", "reward_name", "reward_cost"})
		if (obs_property_t *property = obs_properties_get(properties, keyed(key, field).c_str()))
			obs_property_set_visible(property, reward);
	return true;
}

bool createPressed(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<ChatTrigger *>(data)->createReward();
	return false; // drawn again once Twitch has answered
}

} // namespace

std::string commandName(const std::string &typed)
{
	std::string name;
	const size_t first = typed.find_first_not_of(" !");
	if (first != std::string::npos)
		name = typed.substr(first, typed.find_last_not_of(' ') + 1 - first);
	for (char &c : name)
		c = lower(c);
	const bool valid = !name.empty() && name.size() <= kLongestCommand &&
			   std::all_of(name.begin(), name.end(), [](char c) {
				   return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			   });
	return valid ? name : std::string();
}

QString chatterName(const sarxina_chat_message &message)
{
	const QString name = plainText(utf8(message.display_name), kLongestChatter);
	return name.isEmpty() ? plainText(utf8(message.user_name), kLongestChatter) : name;
}

// Its JSON goes as text both ways; the answer comes on OBS's UI thread.
bool sendToTwitch(const char *method, const QString &path, const QByteArray &body, TwitchAnswer done)
{
	if (!obs_initialized()) // a self-test without OBS
		return false;
	struct Pending {
		TwitchAnswer done;
	};
	const QByteArray where = path.toUtf8();
	auto *pending = new Pending{std::move(done)};
	const sarxina_twitch_request request{method, where.constData(), body.isEmpty() ? nullptr : body.constData(),
					     [](void *param, int status, const char *answer) {
						     std::unique_ptr<Pending> waiting(static_cast<Pending *>(param));
						     waiting->done(status, QJsonDocument::fromJson(answer ? answer : "{}").object());
					     },
					     pending};
	if (!sarxina_obs_plugin_manager_twitch_request(&request)) {
		delete pending;
		return false;
	}
	return true;
}

// ---- the trigger ----------------------------------------------------------------------------------------------------

QString ChatTrigger::text(const char *name) const
{
	if (!strings.empty()) {
		const std::string own = strings + name;
		const char *words = obs_module_text(own.c_str());
		if (words && own != words) // obs_module_text gives the key back when there is no such string
			return QString::fromUtf8(words);
	}
	return QString::fromUtf8(obs_module_text(name));
}

ChatTrigger::ChatTrigger(const std::string &command, TriggerSettings::Trigger trigger, bool cooldownPerChatter)
	: perChatter_(cooldownPerChatter)
{
	settings_.command = command;
	settings_.trigger = trigger;
}

ChatTrigger::~ChatTrigger()
{
	*alive_ = false; // answers from Twitch that come later are dropped
}

void ChatTrigger::setSettings(const TriggerSettings &settings)
{
	if (settings.trigger != settings_.trigger || settings.rewardId != settings_.rewardId)
		waiting_.clear(); // they were for the old trigger
	settings_ = settings;
}

bool ChatTrigger::coolingDown(const sarxina_chat_message &message, double nowMs) const
{
	const double wait = settings_.cooldownSeconds * 1000.0;
	if (!perChatter_)
		return nowMs - lastTime_ < wait;
	const auto last = lastBy_.find(chatterKey(message));
	return last != lastBy_.end() && nowMs - last->second < wait;
}

void ChatTrigger::wentFor(const sarxina_chat_message &message, double nowMs)
{
	lastTime_ = nowMs;
	if (!perChatter_)
		return;
	if (lastBy_.size() >= kChattersKept) {
		const double wait = settings_.cooldownSeconds * 1000.0;
		for (auto at = lastBy_.begin(); at != lastBy_.end();)
			at = nowMs - at->second >= wait ? lastBy_.erase(at) : std::next(at);
	}
	lastBy_[chatterKey(message)] = nowMs;
}

bool ChatTrigger::hear(const sarxina_chat_message &message, double nowMs, bool ready, Heard &heard)
{
	rewardsChanged_ = false;
	const std::string reward = message.reward_id ? message.reward_id : "";
	if (!reward.empty())
		seen(message);

	if (settings_.trigger == TriggerSettings::Trigger::Command) {
		QString rest;
		if (!isCommand(message.text, settings_.command, rest) || !ready || coolingDown(message, nowMs))
			return false;
		wentFor(message, nowMs);
		heard = {chatterName(message), rest, message.user_id ? message.user_id : "", {}};
		return true;
	}

	if (reward.empty() || reward != settings_.rewardId)
		return false;
	if (waiting_.size() < kMostWaiting)
		waiting_.push_back({chatterName(message), utf8(message.text).trimmed(), message.user_id ? message.user_id : "",
				    reward});
	return next(nowMs, ready, heard);
}

bool ChatTrigger::hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who)
{
	Heard heard;
	if (!hear(message, nowMs, ready, heard))
		return false;
	who = heard.who;
	return true;
}

bool ChatTrigger::next(double nowMs, bool ready, Heard &heard)
{
	// With a cooldown for each chatter, the reward's own cooldown on Twitch holds redeems back.
	if (waiting_.empty() || !ready || (!perChatter_ && nowMs - lastTime_ < settings_.cooldownSeconds * 1000.0))
		return false;
	heard = waiting_.front();
	waiting_.pop_front();
	lastTime_ = nowMs;
	return true;
}

bool ChatTrigger::next(double nowMs, bool ready, QString &who)
{
	Heard heard;
	if (!next(nowMs, ready, heard))
		return false;
	who = heard.who;
	return true;
}

void ChatTrigger::seen(const sarxina_chat_message &message)
{
	const std::string id = message.reward_id;
	const QString label = QStringLiteral("%1: %2").arg(chatterName(message),
							   plainText(utf8(message.text), kLongestRedeemText));
	const auto found =
		std::find_if(rewards_.begin(), rewards_.end(), [&id](const Reward &reward) { return reward.id == id; });
	if (found != rewards_.end())
		rewards_.erase(found);
	else
		rewardsChanged_ = true;
	rewards_.insert(rewards_.begin(), {id, label});
	if (rewards_.size() > kRewardsKept)
		rewards_.pop_back();
}

// ---- the channel's rewards ------------------------------------------------------------------------------------------

bool ChatTrigger::ask(const char *method, const QString &path, const QByteArray &body, TwitchAnswer done)
{
	std::weak_ptr<bool> alive = alive_;
	return twitch && twitch(method, path, body, [alive, done](int status, const QJsonObject &answer) {
		       if (const auto still = alive.lock(); still && *still)
			       done(status, answer);
	       });
}

void ChatTrigger::tell()
{
	if (changed)
		changed();
}

void ChatTrigger::listRewards(bool now)
{
	const double at = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
	if (listing_ || (!now && at - listedAt_ < kListAgainMs))
		return;
	listedAt_ = at;
	// Every reward, then the ones the manager made (only those can be marked fulfilled).
	listing_ = ask("GET", QStringLiteral("/channel_points/custom_rewards"), {}, [this](int status, const QJsonObject &all) {
		if (status != 200) {
			listing_ = false;
			const QString was = status_;
			const bool hadList = listed_;
			listed_ = false;
			channel_.clear();
			status_ = why(status, all);
			if (status_ != was || hadList)
				tell();
			return;
		}
		std::vector<ChannelReward> rewards;
		for (const QJsonValue &value : all.value(QStringLiteral("data")).toArray()) {
			const QJsonObject reward = value.toObject();
			rewards.push_back({reward.value(QStringLiteral("id")).toString().toStdString(),
					   plainText(reward.value(QStringLiteral("title")).toString(), kLongestRewardName),
					   reward.value(QStringLiteral("is_user_input_required")).toBool(), false});
		}
		const bool asked = ask("GET", QStringLiteral("/channel_points/custom_rewards?only_manageable_rewards=true"), {},
				       [this, rewards](int status, const QJsonObject &made) mutable {
					       listing_ = false;
					       if (status == 200)
						       for (const QJsonValue &value : made.value(QStringLiteral("data")).toArray()) {
							       const std::string id =
								       value.toObject().value(QStringLiteral("id")).toString().toStdString();
							       for (ChannelReward &reward : rewards)
								       reward.made = reward.made || reward.id == id;
						       }
					       const bool same = listed_ && rewards.size() == channel_.size() &&
								 std::equal(rewards.begin(), rewards.end(), channel_.begin(),
									    [](const ChannelReward &a, const ChannelReward &b) {
										    return a.id == b.id && a.title == b.title &&
											   a.asksForText == b.asksForText &&
											   a.made == b.made;
									    });
					       const bool hadProblem = !status_.isEmpty() && !listed_;
					       channel_ = std::move(rewards);
					       listed_ = true;
					       if (hadProblem)
						       status_.clear();
					       if (!same || hadProblem)
						       tell();
				       });
		if (!asked)
			listing_ = false;
	});
	if (!listing_) { // the manager is not installed, or is a version without Twitch's API
		const bool news = listed_ || status_ != this->text("Reward.NoManager");
		listed_ = false;
		channel_.clear();
		status_ = this->text("Reward.NoManager");
		if (news)
			tell();
	}
}

void ChatTrigger::createReward()
{
	const QString title = plainText(this->text("Reward.Title"), kLongestRewardName);
	QJsonObject reward{
		{QStringLiteral("title"), title},
		{QStringLiteral("cost"), newRewardCost},
		{QStringLiteral("prompt"), plainText(this->text("Reward.Prompt"), kLongestPrompt)},
		{QStringLiteral("is_enabled"), true},
		{QStringLiteral("is_user_input_required"), true}, // only a reward that asks for text comes through chat
		{QStringLiteral("is_global_cooldown_enabled"), true},
		{QStringLiteral("global_cooldown_seconds"), settings_.cooldownSeconds},
		{QStringLiteral("should_redemptions_skip_request_queue"), false},
	};
	const bool sent = ask("POST", QStringLiteral("/channel_points/custom_rewards"),
			      QJsonDocument(reward).toJson(QJsonDocument::Compact), [this, title](int status, const QJsonObject &answer) {
				      const QJsonObject made = answer.value(QStringLiteral("data")).toArray().first().toObject();
				      const std::string id = made.value(QStringLiteral("id")).toString().toStdString();
				      if (status != 200 || id.empty()) {
					      status_ = why(status, answer);
					      tell();
					      return;
				      }
				      TriggerSettings picked = settings_;
				      picked.trigger = TriggerSettings::Trigger::Reward;
				      picked.rewardId = id;
				      picked.rewardLabel = title;
				      if (picked.rewardName.isEmpty())
					      picked.rewardName = title;
				      setSettings(picked);
				      channel_.push_back({id, title, true, true});
				      status_ = this->text("Reward.Created").arg(title);
				      tell();
				      listRewards(true);
			      });
	if (!sent) {
		status_ = this->text("Reward.NoManager");
		tell();
	}
}

void ChatTrigger::fulfil(const Heard &heard)
{
	if (heard.rewardId.empty())
		return;
	const auto reward = std::find_if(channel_.begin(), channel_.end(),
					 [&heard](const ChannelReward &r) { return r.id == heard.rewardId; });
	if (reward == channel_.end() || !reward->made)
		return; // Twitch lets only the manager's own rewards be marked
	const std::string rewardId = heard.rewardId;
	const QString path = QStringLiteral("/channel_points/custom_rewards/redemptions?") + query("reward_id", rewardId) +
			     QStringLiteral("&status=UNFULFILLED&sort=OLDEST&first=50");
	ask("GET", path, {}, [this, heard, rewardId](int status, const QJsonObject &answer) {
		if (status != 200)
			return;
		// The oldest waiting redeem by that chatter with that text: one redeem, one mark.
		for (const QJsonValue &value : answer.value(QStringLiteral("data")).toArray()) {
			const QJsonObject redemption = value.toObject();
			const std::string id = redemption.value(QStringLiteral("id")).toString().toStdString();
			if (id.empty() || fulfilled_.count(id) ||
			    redemption.value(QStringLiteral("user_id")).toString().toStdString() != heard.userId ||
			    redemption.value(QStringLiteral("user_input")).toString().trimmed() != heard.text)
				continue;
			fulfilled_.insert(id);
			const QString mark = QStringLiteral("/channel_points/custom_rewards/redemptions?") +
					     query("reward_id", rewardId) + QLatin1Char('&') + query("id", id);
			ask("PATCH", mark, QByteArrayLiteral("{\"status\":\"FULFILLED\"}"), [](int, const QJsonObject &) {});
			return;
		}
	});
}

// ---- the settings ---------------------------------------------------------------------------------------------------

void addTriggerProperties(obs_properties_t *properties, ChatTrigger &trigger, bool rewardName)
{
	const std::string &key = trigger.key;
	const auto name = [&key](const char *field) { return keyed(key, field); };
	const auto words = [&trigger](const char *string) { return utf8(trigger.text(string)); };

	obs_property_t *pick = obs_properties_add_list(properties, name("trigger").c_str(), words("Trigger").constData(),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(pick, words("Trigger.Command").constData(), "command");
	obs_property_list_add_string(pick, words("Trigger.Reward").constData(), "reward");
	obs_property_set_modified_callback2(pick, triggerPicked, &trigger);

	obs_properties_add_text(properties, name("command").c_str(), words("Command").constData(), OBS_TEXT_DEFAULT);

	// The channel's rewards as Twitch lists them; without the manager's Twitch connection, the ones redeemed in chat
	// since OBS started. And the one picked before, which may be neither.
	obs_property_t *reward = obs_properties_add_list(properties, name("reward").c_str(), words("Reward").constData(),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(reward, words("Reward.None").constData(), "");
	const TriggerSettings &now = trigger.settings();
	std::vector<std::pair<std::string, QString>> offered;
	if (trigger.listed()) {
		for (const ChannelReward &r : trigger.channelRewards())
			offered.emplace_back(r.id, r.asksForText ? r.title
							       : r.title + QLatin1Char(' ') + trigger.text("Reward.NoText"));
	} else {
		for (const ChatTrigger::Reward &r : trigger.rewards())
			offered.emplace_back(r.id, r.label);
	}
	const bool listed = now.rewardId.empty() || std::any_of(offered.begin(), offered.end(), [&now](const auto &r) {
				    return r.first == now.rewardId;
			    });
	if (!listed)
		obs_property_list_add_string(reward, utf8(now.rewardLabel).constData(), now.rewardId.c_str());
	for (const auto &[id, label] : offered)
		obs_property_list_add_string(reward, utf8(label).constData(), id.c_str());
	if (trigger.costInSettings) {
		obs_property_t *cost = obs_properties_add_int(properties, name("reward_cost").c_str(),
							      words("RewardCost").constData(), 1, 1000000, 50);
		obs_property_int_set_suffix(cost, utf8(QLatin1Char(' ') + trigger.text("RewardCost.Unit")).constData());
	}
	obs_properties_add_button2(properties, name("reward_create").c_str(), words("Reward.Create").constData(),
				   createPressed, &trigger);
	const QString help = trigger.status().isEmpty() ? trigger.text("Reward.Help") : trigger.status();
	obs_properties_add_text(properties, name("reward_help").c_str(), utf8(help).constData(), OBS_TEXT_INFO);
	if (rewardName)
		obs_properties_add_text(properties, name("reward_name").c_str(), words("RewardName").constData(),
					OBS_TEXT_DEFAULT);

	obs_property_t *cooldown = obs_properties_add_int(properties, name("cooldown").c_str(), words("Cooldown").constData(),
							  kLeastCooldown, kMostCooldown, 1);
	obs_property_int_set_suffix(cooldown, " s");
	obs_property_set_long_description(cooldown, words("Cooldown.Help").constData());

	trigger.listRewards();
}

void triggerToSettings(obs_data_t *settings, const TriggerSettings &now, const std::string &key)
{
	obs_data_set_string(settings, keyed(key, "trigger").c_str(),
			    now.trigger == TriggerSettings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(settings, keyed(key, "command").c_str(), ("!" + now.command).c_str());
	obs_data_set_string(settings, keyed(key, "reward").c_str(), now.rewardId.c_str());
	obs_data_set_string(settings, keyed(key, "reward_name").c_str(), utf8(now.rewardName).constData());
	obs_data_set_int(settings, keyed(key, "cooldown").c_str(), now.cooldownSeconds);
}

TriggerSettings triggerFromSettings(obs_data_t *settings, const ChatTrigger &trigger, bool &rewrite)
{
	const std::string &key = trigger.key;
	TriggerSettings changed = trigger.settings();
	changed.trigger = std::strcmp(obs_data_get_string(settings, keyed(key, "trigger").c_str()), "reward") == 0
				  ? TriggerSettings::Trigger::Reward
				  : TriggerSettings::Trigger::Command;

	const std::string typed = obs_data_get_string(settings, keyed(key, "command").c_str());
	const std::string command = commandName(typed);
	if (!command.empty())
		changed.command = command;
	rewrite = typed != "!" + changed.command;

	changed.rewardName =
		plainText(utf8(obs_data_get_string(settings, keyed(key, "reward_name").c_str())), kLongestRewardName);
	const std::string reward = obs_data_get_string(settings, keyed(key, "reward").c_str());
	if (reward != changed.rewardId) {
		changed.rewardId = reward;
		changed.rewardLabel.clear();
		for (const ChannelReward &r : trigger.channelRewards())
			if (r.id == reward) {
				changed.rewardLabel = r.title;
				// The board shows the reward's name: Twitch's, unless the streamer typed one.
				if (changed.rewardName.isEmpty() || changed.rewardName == trigger.settings().rewardLabel) {
					changed.rewardName = r.title;
					rewrite = true;
				}
			}
		for (const ChatTrigger::Reward &r : trigger.rewards())
			if (r.id == reward && changed.rewardLabel.isEmpty())
				changed.rewardLabel = r.label;
	}
	changed.cooldownSeconds = std::clamp(static_cast<int>(obs_data_get_int(settings, keyed(key, "cooldown").c_str())),
					     kLeastCooldown, kMostCooldown);
	return changed;
}

void saveTrigger(obs_data_t *config, const TriggerSettings &now, const std::string &key)
{
	obs_data_set_string(config, keyed(key, "trigger").c_str(),
			    now.trigger == TriggerSettings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(config, keyed(key, "command").c_str(), now.command.c_str());
	obs_data_set_string(config, keyed(key, "reward_id").c_str(), now.rewardId.c_str());
	obs_data_set_string(config, keyed(key, "reward_label").c_str(), utf8(now.rewardLabel).constData());
	obs_data_set_string(config, keyed(key, "reward_name").c_str(), utf8(now.rewardName).constData());
	obs_data_set_int(config, keyed(key, "cooldown").c_str(), now.cooldownSeconds);
}

TriggerSettings loadTrigger(obs_data_t *config, const TriggerSettings &defaults, const std::string &key)
{
	TriggerSettings settings = defaults;
	if (!config)
		return settings;
	const auto name = [&key](const char *field) { return keyed(key, field); };
	obs_data_set_default_string(config, name("trigger").c_str(),
				    defaults.trigger == TriggerSettings::Trigger::Reward ? "reward" : "command");
	obs_data_set_default_string(config, name("command").c_str(), defaults.command.c_str());
	obs_data_set_default_int(config, name("cooldown").c_str(), defaults.cooldownSeconds);
	settings.trigger = std::strcmp(obs_data_get_string(config, name("trigger").c_str()), "reward") == 0
				   ? TriggerSettings::Trigger::Reward
				   : TriggerSettings::Trigger::Command;
	if (const std::string command = commandName(obs_data_get_string(config, name("command").c_str()));
	    !command.empty())
		settings.command = command;
	settings.rewardId = obs_data_get_string(config, name("reward_id").c_str());
	settings.rewardLabel = utf8(obs_data_get_string(config, name("reward_label").c_str()));
	settings.rewardName =
		plainText(utf8(obs_data_get_string(config, name("reward_name").c_str())), kLongestRewardName);
	settings.cooldownSeconds = std::clamp(static_cast<int>(obs_data_get_int(config, name("cooldown").c_str())),
					      kLeastCooldown, kMostCooldown);
	return settings;
}

} // namespace sarxina
