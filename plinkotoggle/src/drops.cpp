#include "drops.hpp"

#include "plain-text.hpp"

#include <algorithm>

namespace sarxina::plinkotoggle {

namespace {

constexpr int kLongestChatter = 25;   // characters, as long as a Twitch name
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

void Drops::setSettings(const Settings &settings)
{
	if (settings.trigger != settings_.trigger || settings.rewardId != settings_.rewardId)
		waiting_.clear(); // they were for the old trigger
	settings_ = settings;
}

bool Drops::coolingDown(double nowMs) const
{
	return nowMs - lastDrop_ < settings_.cooldownSeconds * 1000.0;
}

bool Drops::hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who)
{
	rewardsChanged_ = false;
	const std::string reward = message.reward_id ? message.reward_id : "";
	if (!reward.empty())
		seen(message);

	if (settings_.trigger == Settings::Trigger::Command) {
		if (!isCommand(message.text, settings_.command) || !ready || coolingDown(nowMs))
			return false;
		lastDrop_ = nowMs;
		who = chatterName(message);
		return true;
	}

	if (reward.empty() || reward != settings_.rewardId)
		return false;
	if (waiting_.size() < kMostWaiting)
		waiting_.push_back(chatterName(message));
	return next(nowMs, ready, who);
}

bool Drops::next(double nowMs, bool ready, QString &who)
{
	if (waiting_.empty() || !ready || coolingDown(nowMs))
		return false;
	who = waiting_.front();
	waiting_.pop_front();
	lastDrop_ = nowMs;
	return true;
}

void Drops::seen(const sarxina_chat_message &message)
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

} // namespace sarxina::plinkotoggle
