// The NPCs ChatSaga's town shows. Each has an id that never changes, a name over its head, its dialogue (what it says
// when the player talks to it), when it last spoke (the game shows a speech icon for a while), its class (an id in the
// game's Classes.json, which sets its stats) and the address of its avatar, shown when it is talked to and on its
// status screen; all but the id can change. The game draws whatever is here
// (town-server.hpp), and anything may change it: chat (chat-presence.hpp) or a caller of the local trigger.
#pragma once

#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace sarxina::chatsaga {

inline constexpr int kLongestId = 32;        // characters
inline constexpr int kLongestName = 32;      // characters
inline constexpr int kLongestDialogue = 500; // characters: as long as a chat message gets

struct Npc {
	QString id;       // npcId(): the same NPC whatever the case it was given in
	QString name;     // npcName()
	QString dialogue; // npcDialogue(); "" for none
	qint64 spokeAt = 0; // when it last spoke, in milliseconds since 1970 UTC; 0 for not lately
	int classId = 0;    // 0: the game picks one from the id
	QString avatar;     // "": the game draws a default
	bool operator==(const Npc &other) const
	{
		return id == other.id && name == other.name && dialogue == other.dialogue && spokeAt == other.spokeAt &&
		       classId == other.classId && avatar == other.avatar;
	}
};

// What a caller wants of an NPC. What is left out stays as it was; a new NPC's name is then its id, it says nothing,
// it has not spoken, and it has no class or avatar of its own.
struct NpcChange {
	std::optional<QString> name;
	std::optional<QString> dialogue;
	std::optional<qint64> spokeAt;
	std::optional<int> classId;
	std::optional<QString> avatar;
};

// The id an NPC is known by, or "" if what was given cannot be one: 1 to kLongestId characters of plain text.
QString npcId(const QString &given);
// The name shown over an NPC: plain text, cut to kLongestName characters; its id when none is given.
QString npcName(const QString &given, const QString &id);
// What an NPC says: one line of plain text, cut to kLongestDialogue characters.
QString npcDialogue(const QString &given);

class Town {
public:
	enum class Put { Added, Changed, Unchanged };

	// Told of every change, for the game.
	std::function<void(const Npc &)> shown; // an NPC came, or something of it changed
	std::function<void(const QString &id)> gone;

	// Puts an NPC in town, or changes the one there as asked. `id` is npcId() already.
	Put put(const QString &id, const NpcChange &change);
	// Takes the NPC out of town. False if it was not there.
	bool remove(const QString &id);

	const Npc *find(const QString &id) const;
	// In the order they came.
	const std::vector<Npc> &npcs() const { return npcs_; }

private:
	std::vector<Npc> npcs_;
};

} // namespace sarxina::chatsaga
