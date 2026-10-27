// Each chatter's lasting profile in the town: the class they were given the first time they came, kept from stream to
// stream in a JSON file of the plugin's own, by Twitch user id. The class is picked at random, but always the same one
// for the same user id, so a profile that is lost comes back as it was.
//
// The profile is all the game needs to know of a chatter beyond chat: a hosted service that lets chatters choose their
// class could stand in for this file later, with the town asking it instead.
#pragma once

#include <QString>

#include <map>

namespace sarxina::chatsaga {

inline constexpr int kClassCount = 8; // the classes in the game's Classes.json: 1 to kClassCount

// The class a chatter names in "!chatsaga changeclass <class>", in any case and with or without spaces: 1 to
// kClassCount, in the order of the game's Classes.json, or 0 for none.
int classNamed(const QString &name);

// The class picked for a user id the first time it is seen.
int firstClassFor(const QString &userId);

class Profiles {
public:
	// Profiles kept in `file`, or only for this run when it is empty.
	explicit Profiles(const QString &file);

	// The chatter's class: as kept, or picked and kept now.
	int classFor(const QString &userId);
	// The chatter chose a class (1 to kClassCount): kept from now on.
	void setClass(const QString &userId, int classId);
	size_t size() const { return classes_.size(); }

private:
	void load();
	void save() const;

	QString file_;
	std::map<QString, int> classes_; // user id -> class
};

} // namespace sarxina::chatsaga
