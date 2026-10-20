#include "chat-trigger.hpp"

#include "plain-text.hpp"

#include <obs-module.h>

#include <algorithm>
#include <cstring>

namespace sarxina {

namespace {

constexpr int kLongestChatter = 25;    // characters, as long as a Twitch name
constexpr int kLongestRedeemText = 30; // characters of what the viewer wrote, in the settings' list of rewards
constexpr size_t kLongestCommand = 25;
constexpr size_t kRewardsKept = 20;

char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// "!plinko" or "!plinko and more", in any case.
bool isCommand(const char *text, const std::string &command)
{
	if (!text || text[0] != '!' || command.empty())
		return false;
	for (size_t i = 0; i < command.size(); i++)
		if (text[i + 1] == '\0' || lower(text[i + 1]) != command[i])
			return false;
	const char after = text[command.size() + 1];
	return after == '\0' || after == ' ';
}

QString utf8(const char *text)
{
	return QString::fromUtf8(text ? text : "");
}

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

// The settings show the command field or the reward fields, whichever the trigger needs.
bool triggerPicked(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool reward = std::strcmp(obs_data_get_string(settings, "trigger"), "reward") == 0;
	obs_property_set_visible(obs_properties_get(properties, "command"), !reward);
	for (const char *field : {"reward", "reward_help", "reward_name"})
		if (obs_property_t *property = obs_properties_get(properties, field))
			obs_property_set_visible(property, reward);
	return true;
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

// ---- the trigger ----------------------------------------------------------------------------------------------------

ChatTrigger::ChatTrigger(const std::string &command)
{
	settings_.command = command;
}

void ChatTrigger::setSettings(const TriggerSettings &settings)
{
	if (settings.trigger != settings_.trigger || settings.rewardId != settings_.rewardId)
		waiting_.clear(); // they were for the old trigger
	settings_ = settings;
}

bool ChatTrigger::coolingDown(double nowMs) const
{
	return nowMs - lastTime_ < settings_.cooldownSeconds * 1000.0;
}

bool ChatTrigger::hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who)
{
	rewardsChanged_ = false;
	const std::string reward = message.reward_id ? message.reward_id : "";
	if (!reward.empty())
		seen(message);

	if (settings_.trigger == TriggerSettings::Trigger::Command) {
		if (!isCommand(message.text, settings_.command) || !ready || coolingDown(nowMs))
			return false;
		lastTime_ = nowMs;
		who = chatterName(message);
		return true;
	}

	if (reward.empty() || reward != settings_.rewardId)
		return false;
	if (waiting_.size() < kMostWaiting)
		waiting_.push_back(chatterName(message));
	return next(nowMs, ready, who);
}

bool ChatTrigger::next(double nowMs, bool ready, QString &who)
{
	if (waiting_.empty() || !ready || coolingDown(nowMs))
		return false;
	who = waiting_.front();
	waiting_.pop_front();
	lastTime_ = nowMs;
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

// ---- the settings ---------------------------------------------------------------------------------------------------

void addTriggerProperties(obs_properties_t *properties, const ChatTrigger &trigger, bool rewardName)
{
	obs_property_t *pick = obs_properties_add_list(properties, "trigger", obs_module_text("Trigger"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(pick, obs_module_text("Trigger.Command"), "command");
	obs_property_list_add_string(pick, obs_module_text("Trigger.Reward"), "reward");
	obs_property_set_modified_callback(pick, triggerPicked);

	obs_properties_add_text(properties, "command", obs_module_text("Command"), OBS_TEXT_DEFAULT);

	// The rewards redeemed in chat since OBS started, and the one picked before, which may not have been.
	obs_property_t *reward = obs_properties_add_list(properties, "reward", obs_module_text("Reward"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(reward, obs_module_text("Reward.None"), "");
	const TriggerSettings &now = trigger.settings();
	const std::vector<ChatTrigger::Reward> &seen = trigger.rewards();
	const bool listed = now.rewardId.empty() || std::any_of(seen.begin(), seen.end(), [&now](const auto &r) {
				    return r.id == now.rewardId;
			    });
	if (!listed)
		obs_property_list_add_string(reward, utf8(now.rewardLabel).constData(), now.rewardId.c_str());
	for (const ChatTrigger::Reward &r : seen)
		obs_property_list_add_string(reward, utf8(r.label).constData(), r.id.c_str());
	obs_properties_add_text(properties, "reward_help", obs_module_text("Reward.Help"), OBS_TEXT_INFO);
	if (rewardName)
		obs_properties_add_text(properties, "reward_name", obs_module_text("RewardName"), OBS_TEXT_DEFAULT);

	obs_property_t *cooldown =
		obs_properties_add_int(properties, "cooldown", obs_module_text("Cooldown"), kLeastCooldown, kMostCooldown, 1);
	obs_property_int_set_suffix(cooldown, " s");
	obs_property_set_long_description(cooldown, obs_module_text("Cooldown.Help"));
}

void triggerToSettings(obs_data_t *settings, const TriggerSettings &now)
{
	obs_data_set_string(settings, "trigger", now.trigger == TriggerSettings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(settings, "command", ("!" + now.command).c_str());
	obs_data_set_string(settings, "reward", now.rewardId.c_str());
	obs_data_set_string(settings, "reward_name", utf8(now.rewardName).constData());
	obs_data_set_int(settings, "cooldown", now.cooldownSeconds);
}

TriggerSettings triggerFromSettings(obs_data_t *settings, const ChatTrigger &trigger, bool &rewrite)
{
	TriggerSettings changed = trigger.settings();
	changed.trigger = std::strcmp(obs_data_get_string(settings, "trigger"), "reward") == 0
				  ? TriggerSettings::Trigger::Reward
				  : TriggerSettings::Trigger::Command;

	const std::string typed = obs_data_get_string(settings, "command");
	const std::string command = commandName(typed);
	if (!command.empty())
		changed.command = command;
	rewrite = typed != "!" + changed.command;

	const std::string reward = obs_data_get_string(settings, "reward");
	if (reward != changed.rewardId) {
		changed.rewardId = reward;
		changed.rewardLabel.clear();
		for (const ChatTrigger::Reward &r : trigger.rewards())
			if (r.id == reward)
				changed.rewardLabel = r.label;
	}
	changed.rewardName = plainText(utf8(obs_data_get_string(settings, "reward_name")), kLongestRewardName);
	changed.cooldownSeconds =
		std::clamp(static_cast<int>(obs_data_get_int(settings, "cooldown")), kLeastCooldown, kMostCooldown);
	return changed;
}

void saveTrigger(obs_data_t *config, const TriggerSettings &now)
{
	obs_data_set_string(config, "trigger", now.trigger == TriggerSettings::Trigger::Reward ? "reward" : "command");
	obs_data_set_string(config, "command", now.command.c_str());
	obs_data_set_string(config, "reward_id", now.rewardId.c_str());
	obs_data_set_string(config, "reward_label", utf8(now.rewardLabel).constData());
	obs_data_set_string(config, "reward_name", utf8(now.rewardName).constData());
	obs_data_set_int(config, "cooldown", now.cooldownSeconds);
}

TriggerSettings loadTrigger(obs_data_t *config, const TriggerSettings &defaults)
{
	TriggerSettings settings = defaults;
	if (!config)
		return settings;
	obs_data_set_default_string(config, "trigger", "command");
	obs_data_set_default_string(config, "command", defaults.command.c_str());
	obs_data_set_default_int(config, "cooldown", defaults.cooldownSeconds);
	if (std::strcmp(obs_data_get_string(config, "trigger"), "reward") == 0)
		settings.trigger = TriggerSettings::Trigger::Reward;
	if (const std::string command = commandName(obs_data_get_string(config, "command")); !command.empty())
		settings.command = command;
	settings.rewardId = obs_data_get_string(config, "reward_id");
	settings.rewardLabel = utf8(obs_data_get_string(config, "reward_label"));
	settings.rewardName = plainText(utf8(obs_data_get_string(config, "reward_name")), kLongestRewardName);
	settings.cooldownSeconds =
		std::clamp(static_cast<int>(obs_data_get_int(config, "cooldown")), kLeastCooldown, kMostCooldown);
	return settings;
}

} // namespace sarxina
