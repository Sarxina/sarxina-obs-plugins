// The town as anything on this computer reaches it, on 127.0.0.1:kPort (local-http.hpp): the town's page, the trigger
// that puts NPCs in town and takes them out, and the feed the page draws them from.
//
//   GET    /              the town's page, and the game's other files under it (town-page.hpp)
//   GET    /npcs          {"npcs": [{"id", "name", "dialogue", "spokeAt", "classId", "avatar"}, ...]}, in the order
//                         they came. spokeAt is when the NPC last spoke, in milliseconds since 1970 UTC, or 0;
//                         classId is its class in the game, or 0 to let the game pick; avatar is a picture's address,
//                         or "" for the game's default.
//   PUT    /npcs/<id>     puts the NPC in town, or changes the one there. The body has any of those fields but "id",
//                         or is empty; what is left out stays as it was (a new NPC is named after its id, says
//                         nothing, has not spoken, and has no class or avatar of its own). 201 with the NPC and
//                         "added": true, or 200 with "added": false when it was there already.
//   GET    /avatars/<file> a chatter's avatar, from the folder setAvatars() names
//   DELETE /npcs/<id>     takes it out: 200 {"id", "removed": true}, or 404 with "removed": false
//   GET    /npcs/events   the feed, as an event stream: "town" with every NPC as GET /npcs gives them, then "npc"
//                         (the NPC) each time one comes or changes, "gone" ({"id"}), and "status" ({"id"}) when a
//                         chatter asks to see their status on stream.
//
// The id is URL-encoded, 1 to 32 characters, and the same in any case. Anything else is {"error": "..."}: 400 for an
// id or body that cannot be one, 404 for another path, 405 for another method.
#pragma once

#include "local-http.hpp"
#include "town-page.hpp"
#include "town.hpp"

namespace sarxina::chatsaga {

inline constexpr quint16 kPort = 4790;

class TownServer {
public:
	explicit TownServer(Town &town);

	// Starts answering. False when it cannot, with error() saying why.
	bool start(quint16 port = kPort);
	void stop();
	bool running() const { return http_.listening(); }
	QString error() const { return http_.error(); }
	// How many games are drawing the town now.
	int games() const { return http_.streams(); }
	// The game's folder, whose files make the town's page.
	void setPage(const QString &folder) { page_ = folder; }
	// The folder of avatars served under /avatars/.
	void setAvatars(const QString &folder) { avatars_ = folder; }

private:
	HttpAnswer answer(const HttpRequest &request);
	HttpAnswer list() const;
	HttpAnswer feed() const;
	HttpAnswer put(const QString &id, const QByteArray &body);
	HttpAnswer remove(const QString &id);

	Town &town_;
	LocalHttp http_;
	QString page_;
	QString avatars_;
};

} // namespace sarxina::chatsaga
