// When chat sets a plugin off (PlinkoToggle drops a ball, JobApplication brings its form up or takes it down), and who
// did it. The trigger is one of two, picked in the plugin's settings:
//   - a chat command: "!plinko" (or whatever it is set to), in any case, alone or with words after it. One that comes
//     while the plugin is cooling down after the last time, or while it cannot go now, is ignored, like any command.
//   - a channel point reward. Twitch shows a reward in chat only when it asks the viewer for text, as that text with
//     the reward's id. A redeem waits its turn instead of being ignored, so nobody loses their points: one goes each
//     time the cooldown is over and the plugin can go.
// The rewards redeemed in chat are remembered (only while OBS runs), so the settings can offer them to pick from.
//
// Chat comes from the Sarxina OBS Plugin Manager, which has checked it already (sarxina-obs-plugin-manager.h).
// The settings' fields are drawn by the manager from the properties below; their words are each plugin's own strings:
// Trigger, Trigger.Command, Trigger.Reward, Command, Reward, Reward.None, Reward.Help, Cooldown, Cooldown.Help, and
// RewardName for a plugin that shows the reward's name.
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <QString>

#include <deque>
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

class ChatTrigger {
public:
	struct Reward {
		std::string id;
		QString label; // who redeemed it last, and what they wrote
	};

	// `command` is the plugin's own command until the settings say otherwise.
	explicit ChatTrigger(const std::string &command);

	void setSettings(const TriggerSettings &settings);
	const TriggerSettings &settings() const { return settings_; }

	// A chat message. `ready` says whether the plugin can go now. True if it goes now, for `who`.
	bool hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who);
	// True if a redeem that waited goes now, for `who`.
	bool next(double nowMs, bool ready, QString &who);
	size_t waiting() const { return waiting_.size(); }

	// The rewards redeemed in chat since OBS started, the latest first. `rewardsChanged` says whether the last
	// message added one or changed how one is shown.
	const std::vector<Reward> &rewards() const { return rewards_; }
	bool rewardsChanged() const { return rewardsChanged_; }

private:
	bool coolingDown(double nowMs) const;
	void seen(const sarxina_chat_message &message);

	TriggerSettings settings_;
	double lastTime_ = -1e300;
	std::deque<QString> waiting_;
	std::vector<Reward> rewards_;
	bool rewardsChanged_ = false;
};

// ---- the settings, as the manager draws them and the plugin keeps them --------------------------------------------

// The trigger's fields: the command, or the reward (with its name, if `rewardName`), and the cooldown.
void addTriggerProperties(obs_properties_t *properties, const ChatTrigger &trigger, bool rewardName);
// The values the fields show.
void triggerToSettings(obs_data_t *settings, const TriggerSettings &now);
// The fields as the streamer left them. A command that cannot be one keeps the one before, and `rewrite` says the
// command field should show what the command is now.
TriggerSettings triggerFromSettings(obs_data_t *settings, const ChatTrigger &trigger, bool &rewrite);

// In the plugin's own config.json.
void saveTrigger(obs_data_t *config, const TriggerSettings &now);
TriggerSettings loadTrigger(obs_data_t *config, const TriggerSettings &defaults);

} // namespace sarxina
