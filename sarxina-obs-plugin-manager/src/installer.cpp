#include "installer.hpp"

#include "catalog.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <windows.h>

namespace sarxina {

QString Layout::dll(const QString &id) const
{
	return kind == Kind::ObsFolder ? root + QStringLiteral("/obs-plugins/64bit/%1.dll").arg(id)
				       : root + QStringLiteral("/%1/bin/64bit/%1.dll").arg(id);
}

QString Layout::data(const QString &id) const
{
	return kind == Kind::ObsFolder ? root + QStringLiteral("/data/obs-plugins/") + id
				       : root + QStringLiteral("/%1/data").arg(id);
}

QString Layout::folder(const QString &id) const
{
	return kind == Kind::ObsFolder ? QString() : root + QLatin1Char('/') + id;
}

Layout Layout::forObs()
{
	// OBS hands plugins a relative settings folder exactly when it is portable ("../../config/..."), and an absolute
	// one in the user's AppData otherwise.
	BPtr<char> config(obs_module_config_path(""));
	if (config && QDir::isRelativePath(QString::fromUtf8(config.Get()))) {
		BPtr<char> program(os_get_executable_path_ptr("")); // <OBS folder>/bin/64bit/
		if (!program)
			return {};
		return {Kind::ObsFolder, QDir::cleanPath(QString::fromUtf8(program.Get()) + QStringLiteral("/../.."))};
	}
	BPtr<char> plugins(os_get_program_data_path_ptr("obs-studio/plugins"));
	if (!plugins)
		return {};
	return {Kind::ProgramData, QDir::cleanPath(QString::fromUtf8(plugins.Get()))};
}

std::optional<Layout> Layout::holding(const QString &dll, const QString &id)
{
	const QString path = QDir::cleanPath(QDir::fromNativeSeparators(dll));
	const QString inObsFolder = QStringLiteral("/obs-plugins/64bit/%1.dll").arg(id);
	const QString inProgramData = QStringLiteral("/%1/bin/64bit/%1.dll").arg(id);
	if (path.endsWith(inObsFolder, Qt::CaseInsensitive))
		return Layout{Kind::ObsFolder, path.chopped(inObsFolder.size())};
	if (path.endsWith(inProgramData, Qt::CaseInsensitive))
		return Layout{Kind::ProgramData, path.chopped(inProgramData.size())};
	return std::nullopt;
}

namespace files {

namespace {

const QString kPart = QStringLiteral(".part");
const QString kOld = QStringLiteral(".old");
const QString kUninstalled = QStringLiteral(".uninstalled");

const wchar_t *native(const QString &path)
{
	return reinterpret_cast<const wchar_t *>(path.utf16());
}

// Renames a file in place, over one that is there already (which fails if OBS is running that one).
bool move(const QString &from, const QString &to)
{
	return MoveFileExW(native(QDir::toNativeSeparators(from)), native(QDir::toNativeSeparators(to)),
			   MOVEFILE_REPLACE_EXISTING) != 0;
}

bool removeFile(const QString &path)
{
	return !exists(path) || QFile::remove(path);
}

bool removeFolder(const QString &path)
{
	return !QFileInfo(path).isDir() || QDir(path).removeRecursively();
}

// A file name in a plugin's data folder, one folder level at a time: plain names only.
bool plainPath(const QString &path)
{
	static const QRegularExpression part(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._ -]*$"));
	const QStringList parts = path.split(QLatin1Char('/'));
	for (const QString &name : parts)
		if (!part.match(name).hasMatch() || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
			return false;
	return !parts.isEmpty();
}

QString folderOf(const QString &file)
{
	return QDir::toNativeSeparators(QFileInfo(file).absolutePath());
}

} // namespace

bool exists(const QString &path)
{
	return QFileInfo::exists(path);
}

bool writable(const QString &folder)
{
	QFile probe(QDir(folder).filePath(QStringLiteral(".sarxina-write-check")));
	if (!probe.open(QIODevice::WriteOnly))
		return false;
	probe.close();
	probe.remove();
	return true;
}

bool samePath(const QString &a, const QString &b)
{
	return QDir::cleanPath(QFileInfo(a).absoluteFilePath()).compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()),
									 Qt::CaseInsensitive) == 0;
}

std::optional<std::vector<Placement>> place(const Layout &layout, const QString &id, const std::vector<ZipFile> &zip,
					    QString &problem)
{
	if (layout.root.isEmpty() || !validId(id)) {
		problem = QStringLiteral("nowhere to install it");
		return std::nullopt;
	}
	const QString dllName = QStringLiteral("obs-plugins/64bit/%1.dll").arg(id);
	const QString dataPrefix = QStringLiteral("data/obs-plugins/%1/").arg(id);
	std::vector<Placement> placed;
	const QByteArray *dll = nullptr;
	QSet<QString> seen;
	for (const ZipFile &file : zip) {
		if (seen.contains(file.name.toLower())) {
			problem = QStringLiteral("the zip holds a file twice");
			return std::nullopt;
		}
		seen.insert(file.name.toLower());
		if (file.name == dllName) {
			dll = &file.data;
		} else if (file.name.startsWith(dataPrefix) && plainPath(file.name.mid(dataPrefix.size()))) {
			placed.push_back({layout.data(id) + QLatin1Char('/') + file.name.mid(dataPrefix.size()), &file.data});
		} else {
			problem = QStringLiteral("the zip holds something that is not part of %1").arg(id);
			return std::nullopt;
		}
	}
	if (!dll) {
		problem = QStringLiteral("the zip has no %1.dll").arg(id);
		return std::nullopt;
	}
	placed.push_back({layout.dll(id), dll}); // last: the plugin is only there once its DLL is
	return placed;
}

bool write(const Layout &layout, const QString &id, const std::vector<ZipFile> &zip, bool dllInUse, QString &problem)
{
	const std::optional<std::vector<Placement>> placed = place(layout, id, zip, problem);
	if (!placed)
		return false;
	const std::vector<Placement> &data = *placed;

	// Everything is written beside where it goes first, so that a problem leaves the plugin as it was.
	std::vector<QString> written;
	const auto undo = [&written] {
		for (const QString &part : written)
			QFile::remove(part);
	};
	for (const auto &[target, contents] : data) {
		const QString part = target + kPart;
		QFile out(part);
		if (!QDir().mkpath(QFileInfo(part).absolutePath()) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
		    out.write(*contents) != contents->size() || !out.flush()) {
			out.close();
			QFile::remove(part);
			undo();
			problem = QStringLiteral("cannot write to %1").arg(folderOf(target));
			return false;
		}
		out.close();
		written.push_back(part);
	}

	// Then each one takes the place of the file before it. OBS keeps running the DLL it has: that one moves aside.
	const QString target = layout.dll(id);
	if (dllInUse && exists(target) && !move(target, target + kOld)) {
		undo();
		problem = QStringLiteral("cannot move the running %1.dll aside in %2").arg(id, folderOf(target));
		return false;
	}
	for (const auto &[file, contents] : data) {
		if (!move(file + kPart, file)) {
			undo();
			if (dllInUse && exists(target + kOld) && !exists(target))
				move(target + kOld, target);
			problem = QStringLiteral("cannot write to %1").arg(folderOf(file));
			return false;
		}
	}
	return true;
}

bool remove(const Layout &layout, const QString &id, QString &problem)
{
	if (layout.root.isEmpty() || !validId(id)) {
		problem = QStringLiteral("nowhere to remove it from");
		return false;
	}
	const QString dll = layout.dll(id);
	bool ok;
	if (layout.kind == Layout::Kind::ProgramData)
		ok = removeFolder(layout.folder(id));
	else
		ok = removeFile(dll) && removeFile(dll + kOld) && removeFile(dll + kUninstalled) && removeFolder(layout.data(id));
	if (!ok)
		problem = QStringLiteral("cannot delete it from %1").arg(folderOf(dll));
	return ok;
}

bool retire(const Layout &layout, const QString &id, bool updated, QString &problem)
{
	const QString dll = layout.dll(id);
	bool ok;
	if (updated && exists(dll + kOld)) // the new one never ran
		ok = removeFile(dll) && move(dll + kOld, dll + kUninstalled);
	else
		ok = move(dll, dll + kUninstalled);
	if (!ok)
		problem = QStringLiteral("cannot rename %1.dll in %2").arg(id, folderOf(dll));
	return ok;
}

bool restore(const Layout &layout, const QString &id, QString &problem)
{
	const QString dll = layout.dll(id);
	if (move(dll + kUninstalled, dll))
		return true;
	problem = QStringLiteral("cannot rename %1.dll back in %2").arg(id, folderOf(dll));
	return false;
}

bool finishUninstall(const Layout &layout, const QString &id)
{
	if (layout.root.isEmpty() || !validId(id))
		return true; // nothing it may touch
	const QString dll = layout.dll(id);
	if (!removeFile(dll + kUninstalled) || !removeFile(dll + kOld))
		return false;
	if (exists(dll))
		return true; // installed again since: only the leftovers go
	return layout.kind == Layout::Kind::ProgramData ? removeFolder(layout.folder(id)) : removeFolder(layout.data(id));
}

bool finishUpdate(const Layout &layout, const QString &id)
{
	if (layout.root.isEmpty() || !validId(id))
		return true;
	return removeFile(layout.dll(id) + kOld);
}

QString version(const QString &dll)
{
	const QString path = QDir::toNativeSeparators(dll);
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(native(path), &ignored);
	if (size == 0)
		return {};
	QByteArray block(static_cast<qsizetype>(size), Qt::Uninitialized);
	VS_FIXEDFILEINFO *fixed = nullptr;
	UINT length = 0;
	if (!GetFileVersionInfoW(native(path), 0, size, block.data()) ||
	    !VerQueryValueW(block.data(), L"\\", reinterpret_cast<void **>(&fixed), &length) || !fixed ||
	    length < sizeof(VS_FIXEDFILEINFO))
		return {};
	return QStringLiteral("%1.%2.%3")
		.arg(HIWORD(fixed->dwFileVersionMS))
		.arg(LOWORD(fixed->dwFileVersionMS))
		.arg(HIWORD(fixed->dwFileVersionLS));
}

} // namespace files

} // namespace sarxina
