#include "processes.hpp"

#include <QString>

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <map>

namespace sarxina::keeptalking {

namespace {

std::string utf8(const wchar_t *text)
{
	return QString::fromWCharArray(text).toStdString();
}

bool sameName(const std::string &a, const std::string &b)
{
	return QString::fromStdString(a).compare(QString::fromStdString(b), Qt::CaseInsensitive) == 0;
}

DWORD ownSession()
{
	DWORD session = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &session);
	return session;
}

bool inOwnSession(DWORD pid)
{
	DWORD session = 0;
	return ProcessIdToSessionId(pid, &session) && session == ownSession();
}

// Every process in this session, but this one: its id and exe name.
template<typename Each> void eachProcess(Each each)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	const DWORD self = GetCurrentProcessId();
	for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
		if (entry.th32ProcessID != self && entry.th32ProcessID != 0 && inOwnSession(entry.th32ProcessID))
			each(entry.th32ProcessID, utf8(entry.szExeFile));
	CloseHandle(snapshot);
}

std::string exeOf(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process)
		return {};
	wchar_t path[MAX_PATH * 2];
	DWORD size = static_cast<DWORD>(std::size(path));
	std::string exe;
	if (QueryFullProcessImageNameW(process, 0, path, &size)) {
		const QString whole = QString::fromWCharArray(path, static_cast<int>(size));
		exe = whole.mid(whole.lastIndexOf(QLatin1Char('\\')) + 1).toStdString();
	}
	CloseHandle(process);
	return exe;
}

struct Windows {
	std::map<QString, Program> byName; // by the exe name in lower case
};

BOOL CALLBACK eachWindow(HWND window, LPARAM param)
{
	auto &found = *reinterpret_cast<Windows *>(param);
	if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
	    (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
		return TRUE;
	wchar_t title[256];
	if (GetWindowTextW(window, title, static_cast<int>(std::size(title))) <= 0)
		return TRUE;
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid == GetCurrentProcessId() || !inOwnSession(pid))
		return TRUE;
	const std::string exe = exeOf(pid);
	if (exe.empty() || !mayClose(exe))
		return TRUE;
	const QString key = QString::fromStdString(exe).toLower();
	if (!found.byName.count(key))
		found.byName[key] = Program{exe, utf8(title)};
	return TRUE;
}

} // namespace

std::vector<Program> programsWithWindows()
{
	Windows found;
	EnumWindows(eachWindow, reinterpret_cast<LPARAM>(&found));
	std::vector<Program> programs;
	for (auto &[key, program] : found.byName)
		programs.push_back(program);
	return programs;
}

bool isRunning(const std::string &exe)
{
	bool running = false;
	if (!exe.empty())
		eachProcess([&](DWORD, const std::string &name) { running = running || sameName(name, exe); });
	return running;
}

bool mayClose(const std::string &exe)
{
	static const char *const never[] = {
		"obs64.exe",       "obs32.exe",        "obs-browser-page.exe", "explorer.exe",   "dwm.exe",
		"csrss.exe",       "winlogon.exe",     "wininit.exe",          "lsass.exe",      "services.exe",
		"smss.exe",        "svchost.exe",      "sihost.exe",           "ctfmon.exe",     "fontdrvhost.exe",
		"taskmgr.exe",     "conhost.exe",      "System",               "Registry",       "audiodg.exe",
		"ShellExperienceHost.exe", "StartMenuExperienceHost.exe", "SearchHost.exe", "TextInputHost.exe",
		"ApplicationFrameHost.exe", "SystemSettings.exe", "LockApp.exe",
	};
	if (exe.empty())
		return false;
	return std::none_of(std::begin(never), std::end(never), [&](const char *name) { return sameName(exe, name); });
}

Closed closeProgram(const std::string &exe)
{
	Closed result;
	if (!mayClose(exe))
		return result;
	eachProcess([&](DWORD pid, const std::string &name) {
		if (!sameName(name, exe))
			return;
		result.found++;
		HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
		if (process && TerminateProcess(process, 1)) {
			result.closed++;
		} else {
			wchar_t words[256] = {};
			FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0,
				       words, static_cast<DWORD>(std::size(words)), nullptr);
			result.problem = QString::fromWCharArray(words).trimmed().toStdString();
		}
		if (process)
			CloseHandle(process);
	});
	return result;
}

} // namespace sarxina::keeptalking
