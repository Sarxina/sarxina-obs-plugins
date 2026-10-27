#include "town.hpp"

#include "plain-text.hpp"

#include <algorithm>

namespace sarxina::chatsaga {

QString npcId(const QString &given)
{
	const QString id = plainText(given, kLongestId + 1);
	return id.isEmpty() || id.size() > kLongestId ? QString() : id.toLower();
}

QString npcName(const QString &given, const QString &id)
{
	const QString name = plainText(given, kLongestName);
	return name.isEmpty() ? id : name;
}

QString npcDialogue(const QString &given)
{
	return plainText(given, kLongestDialogue);
}

Town::Put Town::put(const QString &id, const NpcChange &change)
{
	auto found = std::find_if(npcs_.begin(), npcs_.end(), [&id](const Npc &npc) { return npc.id == id; });
	if (found == npcs_.end()) {
		npcs_.push_back({id, npcName(change.name.value_or(QString()), id),
				 npcDialogue(change.dialogue.value_or(QString())), std::max<qint64>(0, change.spokeAt.value_or(0)),
				 std::max(0, change.classId.value_or(0)), change.avatar.value_or(QString())});
		if (shown)
			shown(npcs_.back());
		return Put::Added;
	}
	Npc changed = *found;
	if (change.name)
		changed.name = npcName(*change.name, id);
	if (change.dialogue)
		changed.dialogue = npcDialogue(*change.dialogue);
	if (change.spokeAt)
		changed.spokeAt = std::max<qint64>(0, *change.spokeAt);
	if (change.classId)
		changed.classId = std::max(0, *change.classId);
	if (change.avatar)
		changed.avatar = *change.avatar;
	if (changed == *found)
		return Put::Unchanged;
	*found = changed;
	if (shown)
		shown(*found);
	return Put::Changed;
}

bool Town::askStatus(const QString &id)
{
	if (!find(id))
		return false;
	if (statusAsked)
		statusAsked(id);
	return true;
}

bool Town::remove(const QString &id)
{
	auto found = std::find_if(npcs_.begin(), npcs_.end(), [&id](const Npc &npc) { return npc.id == id; });
	if (found == npcs_.end())
		return false;
	npcs_.erase(found);
	if (gone)
		gone(id);
	return true;
}

const Npc *Town::find(const QString &id) const
{
	auto found = std::find_if(npcs_.begin(), npcs_.end(), [&id](const Npc &npc) { return npc.id == id; });
	return found == npcs_.end() ? nullptr : &*found;
}

} // namespace sarxina::chatsaga
