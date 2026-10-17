// When chat drops a ball, and whose ball it is. The trigger is one of two, picked in the settings:
//   - a chat command: "!plinko" (or whatever it is set to), in any case, alone or with words after it. One that comes
//     while the board is cooling down after a drop, or while no board can take a ball, is ignored, like any command.
//   - a channel point reward. Twitch shows a reward in chat only when it asks the viewer for text, as that text with
//     the reward's id. A redeem waits its turn instead of being ignored, so nobody loses their points: one drops each
//     time the cooldown is over and a board can take a ball.
// The rewards redeemed in chat are remembered (only while OBS runs), so the settings can offer them to pick from.
//
// Chat comes from the Sarxina OBS Plugin Manager, which has checked it already (sarxina-obs-plugin-manager.h).
#pragma once

#include "sarxina-obs-plugin-manager.h"

#include <QString>

#include <deque>
#include <string>
#include <vector>

namespace sarxina::plinkotoggle {

struct Settings {
	enum class Trigger { Command, Reward };
	Trigger trigger = Trigger::Command;
	std::string command = "plinko"; // without the "!"
	std::string rewardId;           // empty: none picked
	QString rewardLabel;            // how the picked reward is shown in the settings
	int cooldownSeconds = 10;
};

inline constexpr int kLeastCooldown = 1;  // seconds
inline constexpr int kMostCooldown = 600; // seconds
inline constexpr size_t kMostWaiting = 50; // redeems

// A command as typed in the settings ("!Plinko", "plinko") as chat types it after the "!", or empty if it cannot be
// one: 1 to 25 of a-z, 0-9 and _, like every Sarxina chat command.
std::string commandName(const std::string &typed);

// The name a chatter is shown by on the board: their display name, else their account name.
QString chatterName(const sarxina_chat_message &message);

class Drops {
public:
	struct Reward {
		std::string id;
		QString label; // who redeemed it last, and what they wrote
	};

	void setSettings(const Settings &settings);
	const Settings &settings() const { return settings_; }

	// A chat message. `ready` says whether a board can take a ball now. True if it drops one now, for `who`.
	bool hear(const sarxina_chat_message &message, double nowMs, bool ready, QString &who);
	// True if a redeem that waited drops now, for `who`.
	bool next(double nowMs, bool ready, QString &who);
	size_t waiting() const { return waiting_.size(); }

	// The rewards redeemed in chat since OBS started, the latest first. `rewardsChanged` says whether the last
	// message added one or changed how one is shown.
	const std::vector<Reward> &rewards() const { return rewards_; }
	bool rewardsChanged() const { return rewardsChanged_; }

private:
	bool coolingDown(double nowMs) const;
	void seen(const sarxina_chat_message &message);

	Settings settings_;
	double lastDrop_ = -1e300;
	std::deque<QString> waiting_;
	std::vector<Reward> rewards_;
	bool rewardsChanged_ = false;
};

} // namespace sarxina::plinkotoggle
