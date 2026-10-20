// When chat sets a plugin off (PlinkoToggle drops a ball, JobApplication writes on its form), who did it and what they
// wrote. The trigger is one of two, picked in the plugin's settings:
//   - a chat command: "!plinko" (or whatever it is set to), in any case, alone or with words after it. One that comes
//     while the plugin is cooling down after the last time, or while it cannot go now, is ignored, like any command.
//     The cooldown is for everyone, or, for a plugin that asks for it, for each chatter on their own.
//   - a channel point reward. Twitch shows a reward in chat only when it asks the viewer for text, as that text with
//     the reward's id. A redeem waits its turn instead of being ignored, so nobody loses their points: one goes each
//     time the cooldown is over and the plugin can go. With a cooldown for each chatter, the reward's own cooldown on
//     Twitch is what holds redeems back, and each one goes as soon as the plugin can.
//
// The settings offer the channel's rewards as Twitch lists them, through the Sarxina OBS Plugin Manager's Twitch
// connection (sarxina-obs-plugin-manager.h), and a button that makes a new one from the plugin's own words, which asks
// the viewer for text and has the plugin's cooldown. Without that connection they offer the rewards redeemed in chat
// since OBS started. A redeem of a reward the manager made can be marked fulfilled once the plugin has done its part.
//
// Chat comes from the manager, which has checked it already. The settings' fields are drawn by the manager from the
// properties below; their words are each plugin's own strings: Trigger, Trigger.Command, Trigger.Reward, Command,
// Reward, Reward.None, Reward.Help, Reward.NoText, Reward.Create, Reward.Created, Reward.NoManager, Reward.Title,
// Reward.Prompt, Cooldown, Cooldown.Help, and RewardName for a plugin that shows the reward's name.
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sarxina {

struct TriggerSettings {
	enum class Trigger { Command, Reward };
	Trigger trigger = Trigger::Command;
	std::string command;  // without the "!"
	std::string rewardId; // empty: none picked
	QString rewardLabel;  // how the picked reward is shown in the settings
	QString rewardName;   // as the streamer typed it, for a plugin that shows it: Twitch's chat gives only the id
	int cooldownSeconds = 10;
};

inline constexpr int kLeastCooldown = 1;      // seconds
inline constexpr int kMostCooldown = 600;     // seconds
inline constexpr size_t kMostWaiting = 50;    // redeems
inline constexpr int kLongestRewardName = 45; // characters, as long as Twitch allows

// A command as typed in the settings ("!Plinko", "plinko") as chat types it after the "!", or empty if it cannot be
// one: 1 to 25 of a-z, 0-9 and _, like every Sarxina chat command.
std::string commandName(const std::string &typed);

// The name a chatter is shown by: their display name, else their account name.
QString chatterName(const sarxina_chat_message &message);

// What set the plugin off.
struct Heard {
	QString who;          // chatterName()
	QString text;         // what followed the command, or what the viewer typed for the reward, as chat had it
	std::string userId;   // Twitch's id for the chatter
	std::string rewardId; // the reward, for a redeem; empty for the command
};

// A reward on the channel, as Twitch lists it.
struct ChannelReward {
	std::string id;
	QString title;
	bool asksForText = false; // only these come through chat
	bool made = false;        // made by the manager, so its redeems can be marked fulfilled
};

// Twitch's API, the way the manager offers it: false if it cannot be sent at all, and then `done` is never called.
using TwitchAnswer = std::function<void(int status, const QJsonObject &answer)>;
using TwitchSend = std::function<bool(const char *method, const QString &path, const QByteArray &body, TwitchAnswer done)>;
bool sendToTwitch(const char *method, const QString &path, const QByteArray &body, TwitchAnswer done);

class ChatTrigger {
public:
	struct Reward {
		std::string id;
		QString label; // who redeemed it last, and what they wrote
	};

	// `command` is the plugin's own command and `trigger` its trigger until the settings say otherwise. With
	// `cooldownPerChatter`, the command's cooldown is for each chatter on their own.
	explicit ChatTrigger(const std::string &command, TriggerSettings::Trigger trigger = TriggerSettings::Trigger::Command,
			     bool cooldownPerChatter = false);
	~ChatTrigger();

	void setSettings(const TriggerSettings &settings);
	const TriggerSettings &settings() const { return settings_; }
	bool cooldownPerChatter() const { return perChatter_; }

	// A chat message. `ready` says whether the plugin can go now. True if it goes now, for `heard`.
	bool hear(const sarxina_chat_message &message, double nowMs, bool ready, Heard &heard);
	bool hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who);
	// True if a redeem that waited goes now.
	bool next(double nowMs, bool ready, Heard &heard);
	bool next(double nowMs, bool ready, QString &who);
	size_t waiting() const { return waiting_.size(); }

	// The rewards redeemed in chat since OBS started, the latest first. `rewardsChanged` says whether the last
	// message added one or changed how one is shown.
	const std::vector<Reward> &rewards() const { return rewards_; }
	bool rewardsChanged() const { return rewardsChanged_; }

	// ---- the channel's rewards, through the manager's Twitch connection ----

	// How the plugin reaches Twitch (sendToTwitch unless the self-test says otherwise), and what it calls when the
	// settings should be drawn again: the rewards were listed, one was made, or Twitch said no.
	TwitchSend twitch = sendToTwitch;
	std::function<void()> changed;
	// The cost of a reward the button makes; its title and prompt are the plugin's Reward.Title and Reward.Prompt.
	int newRewardCost = 300;

	// Asks Twitch for the channel's rewards, unless it was asked a moment ago.
	void listRewards(bool now = false);
	const std::vector<ChannelReward> &channelRewards() const { return channel_; }
	bool listed() const { return listed_; }
	// A line for the settings: why there is no list or why a reward could not be made, in Twitch's or the manager's
	// words, or that one was made; empty when there is nothing to say.
	const QString &status() const { return status_; }
	// The button: makes a reward that asks for text, with the cooldown, and picks it.
	void createReward();
	// A redeem the plugin has done: marked fulfilled on Twitch if the manager made its reward, so the streamer's
	// queue does not fill up with it.
	void fulfil(const Heard &heard);

private:
	bool coolingDown(const sarxina_chat_message &message, double nowMs) const;
	void wentFor(const sarxina_chat_message &message, double nowMs);
	void seen(const sarxina_chat_message &message);
	void tell();
	bool ask(const char *method, const QString &path, const QByteArray &body, TwitchAnswer done);

	TriggerSettings settings_;
	bool perChatter_ = false;
	double lastTime_ = -1e300;
	std::map<std::string, double> lastBy_; // the chatter -> when the command last went for them
	std::deque<Heard> waiting_;
	std::vector<Reward> rewards_;
	bool rewardsChanged_ = false;

	std::vector<ChannelReward> channel_;
	bool listed_ = false;
	bool listing_ = false;
	double listedAt_ = -1e300;
	QString status_;
	std::set<std::string> fulfilled_; // redemptions marked already
	std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// ---- the settings, as the manager draws them and the plugin keeps them --------------------------------------------

// The trigger's fields: the command, or the reward (with its name, if `rewardName`), and the cooldown. Asks Twitch for
// the channel's rewards again, for the next time the settings are drawn.
void addTriggerProperties(obs_properties_t *properties, ChatTrigger &trigger, bool rewardName);
// The values the fields show.
void triggerToSettings(obs_data_t *settings, const TriggerSettings &now);
// The fields as the streamer left them. A command that cannot be one keeps the one before, and `rewrite` says the
// command field should show what the command is now.
TriggerSettings triggerFromSettings(obs_data_t *settings, const ChatTrigger &trigger, bool &rewrite);

// In the plugin's own config.json.
void saveTrigger(obs_data_t *config, const TriggerSettings &now);
TriggerSettings loadTrigger(obs_data_t *config, const TriggerSettings &defaults);

} // namespace sarxina
