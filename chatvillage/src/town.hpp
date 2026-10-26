// The NPCs ChatVillage's town shows. Each has an id that never changes, a name over its head and a line of dialogue
// in a speech bubble above that; the name and the dialogue can change. The game draws whatever is here
// (town-server.hpp), and anything may change it: chat (chat-presence.hpp) or a caller of the local trigger.
#pragma once

#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace sarxina::chatvillage {

inline constexpr int kLongestId = 32;        // characters
inline constexpr int kLongestName = 32;      // characters
inline constexpr int kLongestDialogue = 500; // characters: as long as a chat message gets

struct Npc {
	QString id;       // npcId(): the same NPC whatever the case it was given in
	QString name;     // npcName()
	QString dialogue; // npcDialogue(); "" for none
	bool operator==(const Npc &other) const
	{
		return id == other.id && name == other.name && dialogue == other.dialogue;
	}
};

// What a caller wants of an NPC. What is left out stays as it was; a new NPC's name is then its id, and it says
// nothing.
struct NpcChange {
	std::optional<QString> name;
	std::optional<QString> dialogue;
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
	std::function<void(const Npc &)> shown; // an NPC came, or its name or dialogue changed
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

} // namespace sarxina::chatvillage
