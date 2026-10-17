// Where installed plugins go, and what installing and uninstalling do to the files.
//
// OBS 32.2 on Windows loads plugins from its own folder (obs-plugins\64bit\<id>.dll and data\obs-plugins\<id>\), which
// is in Program Files and needs administrator rights to change, and from C:\ProgramData\obs-studio\plugins\<id>\
// (bin\64bit\<id>.dll and data\), where any user may add a folder. So that is where the manager installs plugins. A
// portable OBS (portable_mode.txt in its folder, or started with --portable) does not look in ProgramData, but its own
// folder belongs to the user, so there they go into the OBS folder, laid out as the release zips are.
//
// A plugin OBS has loaded cannot be deleted while OBS runs, but its file can be renamed. Uninstalling one renames
// <id>.dll to <id>.dll.uninstalled, which OBS does not load, and the rest is deleted when OBS next starts. Updating one
// renames the running <id>.dll to <id>.dll.old, deleted the same way, and the new one loads when OBS next starts.
#pragma once

#include "zip.hpp"

#include <QString>

#include <vector>

namespace sarxina {

struct Layout {
	enum class Kind { ProgramData, ObsFolder };
	Kind kind = Kind::ProgramData;
	QString root; // C:/ProgramData/obs-studio/plugins, or the portable OBS's folder; empty: nowhere to install

	QString dll(const QString &id) const;
	QString data(const QString &id) const;
	// ProgramData: the plugin's own folder, which holds everything of it. ObsFolder: empty.
	QString folder(const QString &id) const;

	// Where this OBS loads plugins from.
	static Layout forObs();
};

namespace files {

// Writes a plugin's files from its zip, which has to hold obs-plugins/64bit/<id>.dll and files under
// data/obs-plugins/<id>/, and nothing else. When `dllInUse`, the <id>.dll there now is the one OBS is running: it is
// renamed to <id>.dll.old instead of being replaced. The DLL is written last, so a plugin is never there half.
bool write(const Layout &layout, const QString &id, const std::vector<ZipFile> &zip, bool dllInUse, QString &problem);

// Deletes a plugin that OBS is not running.
bool remove(const Layout &layout, const QString &id, QString &problem);

// A plugin OBS is running: renames its DLL to <id>.dll.uninstalled, so that OBS does not load it again. After an
// update in the same session the running DLL is <id>.dll.old, and the new <id>.dll, never loaded, is deleted.
bool retire(const Layout &layout, const QString &id, bool updated, QString &problem);
// Takes that back.
bool restore(const Layout &layout, const QString &id, QString &problem);

// When OBS starts: deletes what an uninstall left (everything of the plugin, unless it has been installed again since)
// or what an update left (the old DLL). False if something could not be deleted yet.
bool finishUninstall(const Layout &layout, const QString &id);
bool finishUpdate(const Layout &layout, const QString &id);

bool exists(const QString &path);
bool samePath(const QString &a, const QString &b);
// The version in a DLL's file properties ("1.3.0"), or empty.
QString version(const QString &dll);

} // namespace files

} // namespace sarxina
