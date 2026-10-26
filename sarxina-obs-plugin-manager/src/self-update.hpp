// Putting a new version of the manager in place where OBS's own folder needs administrator rights (Program Files).
// The new files are written to a folder of the manager's own first, and one PowerShell run as administrator, after
// Windows asks the streamer, moves the running DLL aside and copies them in. OBS keeps running the DLL it has, and
// the new one loads when OBS next starts.
#pragma once

#include "installer.hpp"

#include <QString>

#include <functional>
#include <vector>

namespace sarxina {

// Runs a PowerShell script; then `done` is called once, on the UI thread, with its exit code, or -1 when it did not
// run (the streamer said no to Windows, or PowerShell could not start).
using RunScript = std::function<void(const QString &script, std::function<void(int exitCode)> done)>;

namespace selfupdate {

// One file to copy in: from the staging folder to where it goes.
struct Copy {
	QString from;
	QString to;
};

// Writes the files placed by files::place into `staging` instead of where they go, and says what to copy where.
bool stage(const std::vector<files::Placement> &placed, const QString &staging, std::vector<Copy> &copies,
	   QString &problem);

// Where the running DLL goes aside: into the staging folder when it is on the same drive (so OBS's folder keeps no
// leftovers), else beside itself as <dll>.old.
QString asideFor(const QString &dll, const QString &staging);

// The script that moves `dll` to `aside` and copies the files in, DLL last, putting the old DLL back if anything fails.
QString script(const QString &dll, const QString &aside, const std::vector<Copy> &copies);

// Runs a script as administrator, which makes Windows ask the streamer first.
void runElevated(const QString &script, std::function<void(int exitCode)> done);
// Runs a script as this process's user, as the self-test does.
void runPlain(const QString &script, std::function<void(int exitCode)> done);

} // namespace selfupdate

} // namespace sarxina
