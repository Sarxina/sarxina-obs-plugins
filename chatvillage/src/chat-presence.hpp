// Who of the channel's chat is in the town, and what their NPCs say.
//   - Everyone in chat is there: the chatters Twitch lists (twitch-chatters.hpp), and anyone who chats, from their
//     first message on, since Twitch's list takes a few minutes to catch up. Someone Twitch no longer lists leaves,
//     unless they chatted within the last kStillHereMs.
//   - Each NPC is named after its chatter, as chat shows their name. Its dialogue is the last thing they said this
//     stream, and "*lurking*" (shown in italics) until they say something. "This stream" is since the stream went
//     live, or, while the channel is not live, since the plugin started.
//   - The broadcaster is the town's player, not an NPC, and the bots in kBots are left out.
// While Twitch's list cannot be had, the chatters who chatted within kStillHereMs are there.
#pragma once

#include "town.hpp"

#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace sarxina::chatvillage {

inline constexpr double kStillHereMs = 10 * 60 * 1000;
inline const QString kLurking = QStringLiteral("*lurking*");

// Chat bots that answer commands or post alerts, by login: they are not viewers.
extern const QStringList kBots;

// A chatter as Twitch names them: the login, and the name chat shows.
struct Chatter {
	QString login;
	QString name;
};

class ChatPresence {
public:
	// Times are in milliseconds since 1970 UTC; `startedMs` is when the plugin started.
	explicit ChatPresence(double startedMs);

	void setBroadcaster(const QString &login);
	// When the stream went live, or nothing while the channel is not live.
	void setLiveSince(std::optional<double> ms);
	// Everyone Twitch lists in chat now, or nothing when the list cannot be had.
	void setListed(std::optional<std::vector<Chatter>> chatters);
	// Someone said this in chat.
	void heard(const Chatter &chatter, const QString &text, double atMs);

	// The NPCs chat puts in the town now: id the login, named after the chatter, saying what they last said this
	// stream or kLurking.
	std::vector<Npc> npcs(double nowMs) const;

private:
	struct Seen {
		QString name;
		double spokeMs = -1; // when they last chatted; -1 never
		QString lastSaid;    // what they said then
	};

	bool isViewer(const QString &login) const;
	bool isHere(const QString &login, double nowMs) const;
	double streamStartMs() const;
	QString dialogueFor(const QString &login) const;
	void remember(const Chatter &chatter);

	double startedMs_;
	std::optional<double> liveSinceMs_;
	QString broadcaster_;
	std::map<QString, Seen> seen_;       // everyone seen in chat, by login
	std::vector<QString> order_;         // logins, in the order they were first seen
	std::optional<std::set<QString>> listed_;
};

} // namespace sarxina::chatvillage
