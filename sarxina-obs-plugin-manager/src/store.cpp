#include "store.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace sarxina {

namespace {

constexpr const char *kLog = "[Sarxina OBS Plugin Manager]";

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QByteArray utf8(const QString &text)
{
	return text.toUtf8();
}

// What OBS loaded, asked of OBS itself.
Store::Loaded loadedInObs(const QString &id)
{
	const QByteArray name = id.toUtf8();
	obs_module_t *module = obs_get_module(name.constData());
	if (!module)
		return {obs_get_disabled_module(name.constData()) ? Store::Loaded::State::OffInObs : Store::Loaded::State::No,
			{}, {}};
	const char *binary = obs_get_module_binary_path(module);
	if (!binary)
		return {Store::Loaded::State::Yes, {}, {}};
	BPtr<char> absolute(os_get_abs_path_ptr(binary)); // OBS's own plugins are relative to its program folder
	const QString dll = QDir::cleanPath(QString::fromUtf8(absolute ? absolute.Get() : binary));
	return {Store::Loaded::State::Yes, dll, files::version(dll)};
}

} // namespace

Store::Setup Store::Setup::forObs()
{
	BPtr<char> remembered(obs_module_config_path("store.json"));
	return {Source::forObs(), Layout::forObs(), loadedInObs, QString::fromUtf8(remembered ? remembered.Get() : "")};
}

Store::Store(Setup setup, QObject *parent) : QObject(parent), setup_(std::move(setup))
{
	load();
	finishLeftovers();
	if (!setup_.layout.root.isEmpty())
		blog(LOG_INFO, "%s plugins are installed into %s (%s)", kLog,
		     utf8(QDir::toNativeSeparators(setup_.layout.root)).constData(),
		     setup_.layout.kind == Layout::Kind::ObsFolder ? "the folder of this portable OBS" : "ProgramData");
}

// ---- the list -------------------------------------------------------------------------------------------------------

void Store::start()
{
	// Every plugin has loaded by now. What OBS loaded is noted once, before anything can be updated.
	started_ = true;
	for (const Available &plugin : available_)
		loaded(plugin.id);
	emit listChanged();
	refresh();
}

void Store::refresh()
{
	if (closed_ || fetching_)
		return;
	if (setup_.source.empty()) {
		list_ = available_.empty() ? List::Failed : List::Ready;
		emit listChanged();
		return;
	}
	fetching_ = true;
	if (available_.empty()) {
		list_ = List::Waiting;
		emit listChanged();
	}
	fetcher_.get(setup_.source, setup_.source.index, kLargestIndex,
		     [this](bool ok, const QByteArray &body, const QString &problem) {
			     fetching_ = false;
			     if (closed_)
				     return;
			     QStringList problems;
			     std::vector<Available> list;
			     const bool read = ok && readIndex(body, setup_.source, list, problems);
			     for (const QString &line : problems)
				     blog(LOG_WARNING, "%s the list of plugins: %s", kLog, utf8(line).constData());
			     if (read) {
				     available_ = std::move(list);
				     listJson_ = body;
				     list_ = List::Ready;
				     for (const Available &plugin : available_)
					     loaded(plugin.id);
				     save();
				     blog(LOG_INFO, "%s %zu plugins on the list", kLog, available_.size());
			     } else {
				     if (!ok)
					     blog(LOG_INFO, "%s cannot fetch the list of plugins: %s", kLog,
						  utf8(problem).constData());
				     list_ = available_.empty() ? List::Failed : List::Ready;
			     }
			     emit listChanged();
		     });
}

const Available *Store::find(const QString &id) const
{
	for (const Available &plugin : available_)
		if (plugin.id == id)
			return &plugin;
	return nullptr;
}

// ---- one plugin -----------------------------------------------------------------------------------------------------

Store::Loaded Store::loaded(const QString &id)
{
	// While OBS is still loading plugins, one it has not reached yet is not loaded so far: asked again each time.
	if (!started_)
		return setup_.loaded ? setup_.loaded(id) : Loaded();
	auto found = loaded_.find(id);
	if (found == loaded_.end())
		found = loaded_.emplace(id, setup_.loaded ? setup_.loaded(id) : Loaded()).first;
	return found->second;
}

Store::State Store::state(const QString &id)
{
	const Loaded obs = loaded(id);
	const auto found = entries_.find(id);
	const Entry entry = found == entries_.end() ? Entry() : found->second;
	const bool inLayout = !setup_.layout.root.isEmpty() && files::exists(setup_.layout.dll(id));
	const bool ours = obs.state == Loaded::State::Yes && files::samePath(obs.dll, setup_.layout.dll(id));

	State state;
	state.busy = entry.busy;
	state.change = entry.change;
	switch (entry.change) {
	case Change::Installed:
	case Change::Updated: state.installed = true; break;
	case Change::Uninstalled: state.installed = false; break;
	case Change::None: state.installed = obs.state == Loaded::State::Yes || inLayout; break;
	}
	if (const Available *plugin = find(id); plugin && ours && entry.change == Change::None &&
						 newer(plugin->version, obs.version)) {
		state.canUpdate = !entry.busy;
		state.newVersion = plugin->version;
	}
	if (!entry.problem.isEmpty())
		state.note = entry.problem;
	else if (entry.change == Change::None && obs.state == Loaded::State::OffInObs)
		state.note = text("Note.OffInObs");
	else if (started_ && entry.change == Change::None && obs.state == Loaded::State::No && inLayout)
		state.note = text("Note.NotLoaded");
	return state;
}

bool Store::running(const QString &id)
{
	const auto found = entries_.find(id);
	return loaded(id).state == Loaded::State::Yes &&
	       (found == entries_.end() || found->second.change != Change::Uninstalled);
}

bool Store::restartNeeded() const
{
	for (const auto &[id, entry] : entries_)
		if (entry.change != Change::None)
			return true;
	return false;
}

QString Store::nameOf(const QString &id) const
{
	const Available *plugin = find(id);
	return plugin ? plugin->name : id;
}

void Store::install(const QString &id)
{
	Entry &entry = entries_[id];
	if (closed_ || entry.busy)
		return;
	entry.problem.clear();

	// Uninstalled in this session, and OBS still runs it: it only has to be put back.
	if (entry.change == Change::Uninstalled) {
		QString why;
		if (files::restore(setup_.layout, id, why)) {
			entry.change = Change::None;
			leftovers_.erase(id);
			save();
			blog(LOG_INFO, "%s %s: uninstall taken back", kLog, utf8(nameOf(id)).constData());
			emit switchPlugin(id, true);
		} else {
			entry.problem = text("Problem.Files").arg(why);
		}
		emit changed(id);
		return;
	}

	const Available *plugin = find(id);
	if (!plugin) {
		entry.problem = text("Problem.NotListed");
		emit changed(id);
		return;
	}
	download(*plugin);
}

void Store::update(const QString &id)
{
	if (closed_ || !state(id).canUpdate)
		return;
	entries_[id].problem.clear();
	download(*find(id));
}

void Store::download(const Available &plugin)
{
	const QString id = plugin.id;
	entries_[id].busy = true;
	emit changed(id);
	blog(LOG_INFO, "%s downloading %s %s from %s", kLog, utf8(plugin.name).constData(),
	     utf8(plugin.version).constData(), utf8(plugin.url).constData());
	fetcher_.get(setup_.source, plugin.url, kLargestZip,
		     [this, id, sha256 = plugin.sha256](bool ok, const QByteArray &body, const QString &problem) {
			     Entry &entry = entries_[id];
			     entry.busy = false;
			     if (closed_)
				     return;
			     if (!ok)
				     entry.problem = text("Problem.Download").arg(problem);
			     else if (QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() != sha256)
				     entry.problem = text("Problem.Checksum");
			     else
				     installZip(id, body);
			     if (!entry.problem.isEmpty())
				     blog(LOG_WARNING, "%s %s not installed: %s", kLog, utf8(nameOf(id)).constData(),
					  utf8(entry.problem).constData());
			     emit changed(id);
		     });
}

void Store::installZip(const QString &id, const QByteArray &zip)
{
	Entry &entry = entries_[id];
	std::vector<ZipFile> unpacked;
	QString why;
	if (!readZip(zip, unpacked, why)) {
		entry.problem = text("Problem.Zip").arg(why);
		return;
	}
	const Loaded obs = loaded(id);
	const bool runs = obs.state == Loaded::State::Yes;
	// The DLL there now is the one OBS runs, unless an update in this session has already moved that one aside.
	const bool inUse = runs && files::samePath(obs.dll, setup_.layout.dll(id)) && entry.change != Change::Updated;
	if (!files::write(setup_.layout, id, unpacked, inUse, why)) {
		entry.problem = text("Problem.Files").arg(why);
		return;
	}
	entry.change = runs ? Change::Updated : Change::Installed;
	if (runs) {
		leftovers_[id] = QStringLiteral("update");
		save();
	}
	blog(LOG_INFO, "%s %s %s into %s: restart OBS to finish", kLog, utf8(nameOf(id)).constData(),
	     runs ? "updated" : "installed", utf8(QDir::toNativeSeparators(setup_.layout.dll(id))).constData());
}

void Store::uninstall(const QString &id)
{
	Entry &entry = entries_[id];
	if (closed_ || entry.busy)
		return;
	entry.problem.clear();
	const Loaded obs = loaded(id);
	QString why;

	if (obs.state != Loaded::State::Yes) {
		// OBS is not running it (it was installed in this session, or OBS did not load it): the files go now.
		if (files::remove(setup_.layout, id, why)) {
			entry.change = Change::None;
			blog(LOG_INFO, "%s %s deleted", kLog, utf8(nameOf(id)).constData());
		} else {
			entry.problem = text("Problem.Files").arg(why);
		}
	} else if (!files::samePath(obs.dll, setup_.layout.dll(id))) {
		// Installed by hand, somewhere the manager does not install to (OBS's own folder in Program Files).
		entry.problem = text("Problem.ByHand").arg(QDir::toNativeSeparators(QFileInfo(obs.dll).absolutePath()));
	} else if (files::retire(setup_.layout, id, entry.change == Change::Updated, why)) {
		entry.change = Change::Uninstalled;
		leftovers_[id] = QStringLiteral("uninstall");
		save();
		blog(LOG_INFO, "%s %s uninstalled: restart OBS to finish", kLog, utf8(nameOf(id)).constData());
		emit switchPlugin(id, false);
	} else {
		entry.problem = text("Problem.Files").arg(why);
	}
	emit changed(id);
}

void Store::shutDown()
{
	closed_ = true;
}

// ---- remembering ----------------------------------------------------------------------------------------------------

void Store::finishLeftovers()
{
	for (auto it = leftovers_.begin(); it != leftovers_.end();) {
		const bool done = it->second == QLatin1String("update") ? files::finishUpdate(setup_.layout, it->first)
									 : files::finishUninstall(setup_.layout, it->first);
		blog(done ? LOG_INFO : LOG_WARNING, "%s %s %s", kLog, utf8(it->first).constData(),
		     done ? (it->second == QLatin1String("update") ? "updated: the old version deleted"
							   : "uninstalled: its files deleted")
			  : "still has files to delete; trying again next time");
		it = done ? leftovers_.erase(it) : std::next(it);
	}
	save();
}

void Store::load()
{
	if (setup_.remembered.isEmpty())
		return;
	QFile file(setup_.remembered);
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject remembered = QJsonDocument::fromJson(file.readAll()).object();
	const QJsonObject leftovers = remembered.value(QStringLiteral("leftovers")).toObject();
	for (auto it = leftovers.begin(); it != leftovers.end(); ++it)
		if (validId(it.key()) && (it.value() == QLatin1String("update") || it.value() == QLatin1String("uninstall")))
			leftovers_[it.key()] = it.value().toString();

	// The list from last time, read as strictly as a fresh one. A test server's list is never kept.
	listJson_ = remembered.value(QStringLiteral("list")).toString().toUtf8();
	QStringList problems;
	if (!listJson_.isEmpty() && !setup_.source.local)
		readIndex(listJson_, setup_.source, available_, problems);
	if (!available_.empty())
		list_ = List::Ready;
}

void Store::save() const
{
	if (setup_.remembered.isEmpty())
		return;
	QJsonObject leftovers;
	for (const auto &[id, what] : leftovers_)
		leftovers.insert(id, what);
	QJsonObject remembered{{QStringLiteral("leftovers"), leftovers}};
	if (!setup_.source.local && !listJson_.isEmpty())
		remembered.insert(QStringLiteral("list"), QString::fromUtf8(listJson_));

	QDir().mkpath(QFileInfo(setup_.remembered).absolutePath());
	QSaveFile file(setup_.remembered);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(remembered).toJson()) < 0 || !file.commit())
		blog(LOG_WARNING, "%s cannot save %s", kLog, utf8(setup_.remembered).constData());
}

} // namespace sarxina
