// Every Sarxina plugin there is, as the dock offers them: the list on GitHub (catalog.hpp), whether each one is
// installed, and installing, uninstalling and updating them (installer.hpp has what that does to the files). A zip is
// installed only if its SHA-256 matches the list's, and nothing downloaded is ever run: the files are only put where
// OBS loads plugins from.
//
// OBS loads plugins only when it starts and unloads them only when it closes, so every change waits for OBS to
// restart, and the dock says so. Two take effect at once as well: an uninstalled plugin the manager runs is switched
// off, and pressing Install again before the restart takes the uninstall back.
//
// It remembers in store.json, beside the manager's config.json, which uninstalls and updates still have files to
// delete when OBS next starts, and the list from last time, for when GitHub cannot be reached.
#pragma once

#include "catalog.hpp"
#include "fetch.hpp"
#include "installer.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace sarxina {

class Store : public QObject {
	Q_OBJECT

public:
	// What OBS did with a plugin when it started.
	struct Loaded {
		enum class State {
			No,
			Yes,
			OffInObs, // installed, but turned off in OBS's own Plugin Manager (Tools > Plugin Manager)
		};
		State state = State::No;
		QString dll;     // the file OBS runs
		QString version; // its version, or empty
	};
	using Lookup = std::function<Loaded(const QString &id)>;

	struct Setup {
		Source source;      // where the list and the zips come from; empty: nowhere
		Layout layout;      // where plugins go
		Lookup loaded;      // what OBS loaded; when empty, nothing
		QString remembered; // the store.json to keep, or empty for none

		static Setup forObs();
	};

	// A change that waits for OBS to restart.
	enum class Change { None, Installed, Uninstalled, Updated };
	// The list from GitHub.
	enum class List {
		Waiting, // being fetched, and there is none from last time
		Ready,
		Failed, // could not be fetched, and there is none from last time
	};

	// One plugin as the dock shows it.
	struct State {
		bool installed = false; // its button says Uninstall
		bool busy = false;      // being downloaded
		bool canUpdate = false; // the list has a newer version
		QString newVersion;
		Change change = Change::None;
		QString note; // what went wrong, or why OBS is not running it; empty when there is nothing to say
	};

	explicit Store(Setup setup, QObject *parent = nullptr);

	// OBS has finished loading: notes what it loaded, then fetches the list.
	void start();
	// Fetches the list again.
	void refresh();
	List list() const { return list_; }
	const std::vector<Available> &available() const { return available_; }
	const Available *find(const QString &id) const;
	const Layout &layout() const { return setup_.layout; }

	State state(const QString &id);
	// OBS runs it now, and it has not been uninstalled.
	bool running(const QString &id);
	// Something waits for OBS to restart.
	bool restartNeeded() const;

	void install(const QString &id); // after an uninstall in this session: takes the uninstall back
	void uninstall(const QString &id);
	void update(const QString &id);

	// OBS is closing: downloads stop, and nothing more changes.
	void shutDown();

signals:
	void listChanged();
	void changed(const QString &id);
	// A plugin is to stop now because it was uninstalled, or to run again because the uninstall was taken back.
	void switchPlugin(const QString &id, bool on);

private:
	struct Entry {
		Change change = Change::None;
		bool busy = false;
		QString problem;
	};

	Loaded loaded(const QString &id);
	void download(const Available &plugin);
	void installZip(const QString &id, const QByteArray &zip);
	void finishLeftovers();
	void load();
	void save() const;
	QString nameOf(const QString &id) const;

	Setup setup_;
	Fetcher fetcher_;
	std::vector<Available> available_;
	QByteArray listJson_; // as fetched, to remember
	List list_ = List::Waiting;
	bool fetching_ = false;
	bool started_ = false; // OBS has finished loading
	bool closed_ = false;
	std::map<QString, Entry> entries_;
	std::map<QString, Loaded> loaded_; // asked once OBS has loaded: that does not change while it runs
	std::map<QString, QString> leftovers_; // id: "uninstall" or "update", still to finish at the next start
};

} // namespace sarxina
