#include "self-update.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QStringList>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

namespace sarxina::selfupdate {

namespace {

// A path as a PowerShell string that nothing in it can end or change.
QString quoted(const QString &path)
{
	QString text = QDir::toNativeSeparators(path);
	text.replace(QLatin1Char('\''), QStringLiteral("''"));
	return QLatin1Char('\'') + text + QLatin1Char('\'');
}

// The program and arguments that run a script in Windows PowerShell, whatever characters it holds.
std::pair<std::wstring, std::wstring> powerShell(const QString &script)
{
	wchar_t windows[MAX_PATH] = {};
	GetSystemDirectoryW(windows, MAX_PATH);
	const std::wstring program = std::wstring(windows) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
	const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), script.size() * 2);
	const QString arguments = QStringLiteral("-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand ") +
				  QString::fromLatin1(utf16.toBase64());
	return {program, arguments.toStdWString()};
}

// Calls `done` with the process's exit code once it has ended, polling on the UI thread.
void whenEnded(HANDLE process, std::function<void(int exitCode)> done)
{
	auto *timer = new QTimer(QCoreApplication::instance());
	QObject::connect(timer, &QTimer::timeout, timer, [timer, process, done = std::move(done)] {
		if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
			return;
		DWORD code = 1;
		GetExitCodeProcess(process, &code);
		CloseHandle(process);
		timer->stop();
		timer->deleteLater();
		done(static_cast<int>(code));
	});
	timer->start(100);
}

// Calls `done` later, on the UI thread, as a run that ended would.
void later(std::function<void(int exitCode)> done, int exitCode)
{
	QTimer::singleShot(0, QCoreApplication::instance(), [done = std::move(done), exitCode] { done(exitCode); });
}

} // namespace

bool stage(const std::vector<files::Placement> &placed, const QString &staging, std::vector<Copy> &copies,
	   QString &problem)
{
	copies.clear();
	QDir(staging).removeRecursively();
	int at = 0;
	for (const files::Placement &file : placed) {
		const QString staged = QDir(staging).filePath(QStringLiteral("files/%1").arg(at++));
		QFile out(staged);
		if (!QDir().mkpath(QFileInfo(staged).absolutePath()) || !out.open(QIODevice::WriteOnly) ||
		    out.write(*file.contents) != file.contents->size() || !out.flush()) {
			problem = QStringLiteral("cannot write to %1").arg(QDir::toNativeSeparators(staging));
			return false;
		}
		copies.push_back({QFileInfo(staged).absoluteFilePath(), file.target});
	}
	return true;
}

QString asideFor(const QString &dll, const QString &staging)
{
	QDir().mkpath(staging);
	const QString name = QFileInfo(dll).fileName() + QStringLiteral(".old");
	if (QStorageInfo(staging).rootPath().compare(QStorageInfo(QFileInfo(dll).absolutePath()).rootPath(),
						     Qt::CaseInsensitive) == 0)
		return QFileInfo(QDir(staging).filePath(name)).absoluteFilePath();
	return dll + QStringLiteral(".old");
}

QString script(const QString &dll, const QString &aside, const std::vector<Copy> &copies)
{
	QStringList lines{
		QStringLiteral("$ErrorActionPreference = 'Stop'"),
		QStringLiteral("$dll = %1").arg(quoted(dll)),
		QStringLiteral("$aside = %1").arg(quoted(aside)),
		QStringLiteral("function Put($from, $to) {"),
		QStringLiteral("  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null"),
		QStringLiteral("  Copy-Item -LiteralPath $from -Destination $to -Force"),
		QStringLiteral("}"),
		QStringLiteral("try {"),
		QStringLiteral("  if (Test-Path -LiteralPath $aside) { Remove-Item -LiteralPath $aside -Force }"),
		QStringLiteral("  Move-Item -LiteralPath $dll -Destination $aside -Force"),
	};
	for (const Copy &copy : copies)
		lines << QStringLiteral("  Put %1 %2").arg(quoted(copy.from), quoted(copy.to));
	lines << QStringLiteral("} catch {")
	      << QStringLiteral("  if (-not (Test-Path -LiteralPath $dll) -and (Test-Path -LiteralPath $aside)) {")
	      << QStringLiteral("    Move-Item -LiteralPath $aside -Destination $dll")
	      << QStringLiteral("  }")
	      << QStringLiteral("  exit 1")
	      << QStringLiteral("}")
	      << QStringLiteral("exit 0");
	return lines.join(QLatin1Char('\n'));
}

void runElevated(const QString &script, std::function<void(int exitCode)> done)
{
	const auto [program, arguments] = powerShell(script);
	SHELLEXECUTEINFOW run{};
	run.cbSize = sizeof(run);
	run.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	run.hwnd = GetForegroundWindow(); // Windows asks over OBS
	run.lpVerb = L"runas";
	run.lpFile = program.c_str();
	run.lpParameters = arguments.c_str();
	run.nShow = SW_HIDE;
	if (!ShellExecuteExW(&run) || !run.hProcess) {
		later(std::move(done), -1);
		return;
	}
	whenEnded(run.hProcess, std::move(done));
}

void runPlain(const QString &script, std::function<void(int exitCode)> done)
{
	const auto [program, arguments] = powerShell(script);
	std::wstring commandLine = L"\"" + program + L"\" " + arguments;
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			    nullptr, &startup, &process)) {
		later(std::move(done), -1);
		return;
	}
	CloseHandle(process.hThread);
	whenEnded(process.hProcess, std::move(done));
}

} // namespace sarxina::selfupdate
