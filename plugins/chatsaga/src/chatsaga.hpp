// ChatSaga on OBS's UI thread, as a plugin of the Sarxina OBS Plugin Manager: a town in RPG Maker MZ, shown in a
// scene as a source (town-source.hpp), where everyone in the channel's chat is an NPC.
//   - Who is in town, and what their NPCs say when talked to, comes from chat (chat-presence.hpp): the chatters Twitch
//     lists, asked through the manager's one Twitch connection (twitch-chatters.hpp), and every message chat sends.
//   - The town is kept here (town.hpp) and handed to the game, and to anything else on this computer, by the local
//     trigger and feed (town-server.hpp).
//   - Its settings behind the manager's gear are one line: how many are in town, or why they cannot be.
// Without the manager there is no chat, and the town holds only what callers of the local trigger put in it.
#pragma once

#include "avatars.hpp"
#include "chat-presence.hpp"
#include "profiles.hpp"
#include "chat-trigger.hpp"
#include "town-server.hpp"
#include "town.hpp"
#include "twitch-chatters.hpp"

#include <obs.h>

#include <QObject>
#include <QSet>
#include <QTimer>

#include <vector>

namespace sarxina::chatsaga {

inline constexpr int kSyncMs = 5000; // how often chatters who went quiet are looked at

class ChatSaga : public QObject {
public:
	// `twitch` reaches Twitch (sendToTwitch unless the self-test says otherwise).
	// Profiles are kept in `profiles` and avatars in `avatars` (neither, for the self-test, when they are empty).
	ChatSaga(const QString &profiles, const QString &avatars, TwitchSend twitch = sendToTwitch);

	// Called once, from obs_module_post_load: the plugin handed to the manager, or run alone.
	void start();
	void shutDown(); // OBS is closing

	// What the manager calls.
	void setEnabled(bool enabled);
	obs_properties_t *properties() const;
	void chat(const sarxina_chat_message &message);

	// A stand-in for Twitch's list of chatters, for a demo or a test (test-route.cpp): Twitch is no longer asked.
	void listChattersForTest(const std::vector<Chatter> &chatters);

	// For the checks (self-test.cpp).
	Town &town() { return town_; }
	TownServer &server() { return server_; }
	bool managed() const { return managed_; }
	bool enabled() const { return enabled_; }
	// The status line in the settings.
	QString status() const;
	// The self-test runs it without OBS or the manager, on its own port.
	void runForTest(quint16 port);

private:
	void turnOn();
	void turnOff();
	void sync();
	QString profileId(const QString &login) const;
	void showStatus();
	double now() const;

	Town town_;
	TownServer server_{town_};
	ChatPresence presence_;
	TwitchChatters twitch_;
	Profiles profiles_;
	Avatars avatars_;
	QSet<QString> fromChat_; // the NPCs chat put in town
	quint16 port_ = kPort;
	bool managed_ = false;
	bool enabled_ = false;
	bool closed_ = false;
	bool testList_ = false;
	QString shownStatus_;
	QTimer tick_;
	QTimer statusLater_;
};

// The test route (test-route.cpp): obs-websocket's vendor requests "chat" and "chatters" of the vendor "ChatSaga",
// added from obs_module_post_load.
void addTestRoute(ChatSaga &plugin);

} // namespace sarxina::chatsaga
