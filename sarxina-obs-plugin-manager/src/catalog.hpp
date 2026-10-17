// The Sarxina plugins anyone can install, and where they may be downloaded from.
//
// The list is plugins.json on the main branch of github.com/Sarxina/sarxina-obs-plugins: each plugin's name, what it
// does, its version, the address of its zip on the repository's releases, and the zip's SHA-256. The manager fetches
// it over HTTPS and takes a zip only from that repository's releases, and only if its SHA-256 matches.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace sarxina {

// Where the list and the zips come from: Sarxina's repository on GitHub, over HTTPS. For tests, a server on this
// computer can stand in for it (the environment variable SARXINA_OBS_PLUGIN_MANAGER_TEST_SERVER), and nothing else.
struct Source {
	QString index;      // the address of plugins.json
	QString downloads;  // the address every zip has to start with
	bool local = false; // a test server on this computer: plain http, and no redirects

	static Source github();
	// http://127.0.0.1:<port>, http://localhost:<port> or http://[::1]:<port>; anything else gives nothing.
	static std::optional<Source> testServer(const QString &address);
	// GitHub, unless SARXINA_OBS_PLUGIN_MANAGER_TEST_SERVER names a test server.
	static Source forObs();

	bool empty() const { return index.isEmpty(); }
};

// One plugin on the list.
struct Available {
	QString id;          // the module's name: "slinglive"
	QString name;        // as shown: "SlingLive"
	QString description; // one sentence
	QString version;     // "1.3.0"
	QString url;         // its zip
	QByteArray sha256;   // the zip's, 64 lower-case hex digits
};

constexpr qint64 kLargestIndex = 1 << 20; // bytes
constexpr qint64 kLargestZip = 64 << 20;

// Reads plugins.json. A plugin that is not right (an id that is not a module name, a zip from anywhere but
// `source.downloads`, no SHA-256, a version that is not x.y.z, the manager itself, one listed twice) is left out and
// named in `problems`. False if the file is not a list of plugins at all.
bool readIndex(const QByteArray &json, const Source &source, std::vector<Available> &list, QStringList &problems);

// A module name the manager installs: 1 to 64 lower-case letters, digits and -, starting with a letter.
bool validId(const QString &id);

// Whether version `a` is newer than `b` ("1.10.0" is newer than "1.9.2"). An unreadable version is never newer.
bool newer(const QString &a, const QString &b);

} // namespace sarxina
