// KeepTalking's self-test (see shared/src/self-test.hpp): the countdown played on made-up times, the word count on
// made-up sound, closing a program
// (a copy of Windows' ping.exe under a name of its own, so nothing else can match it), and the number's picture. No
// OBS, no mic and no sound.
#include "countdown-picture.hpp"
#include "processes.hpp"
#include "self-test.hpp"
#include "silence-countdown.hpp"
#include "word-counter.hpp"

#include <QDir>
#include <QFile>
#include <QString>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using sarxina::keeptalking::SilenceCountdown;
using Event = SilenceCountdown::Event;

struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(bool ok, const std::string &what)
	{
		total++;
		if (ok)
			passed++;
		else if (failed.size() < 400)
			failed += (failed.empty() ? "; failed: " : ", ") + what;
	}
	bool report(const char *what)
	{
		const bool ok = passed == total;
		sarxina::test::report(ok, "%s: %d of %d as expected%s: %s", what, passed, total, failed.c_str(),
				      ok ? "OK" : "FAILED");
		return ok;
	}
};

constexpr double kNever = -1e9;
constexpr double kStep = 0.05; // the plugin's tick

// Steps the countdown from `from` to `to`, and says what happened and the numbers it showed, in order.
struct Run {
	std::vector<std::pair<double, Event>> events;
	std::vector<int> shown;
};
Run play(SilenceCountdown &countdown, double from, double to, bool armed, double lastLoud, double quiet = 5)
{
	Run run;
	for (double now = from; now < to - 1e-9; now += kStep) {
		const Event event = countdown.step(now, armed, lastLoud, quiet);
		if (event != Event::None)
			run.events.push_back({now, event});
		const int number = countdown.shown(now);
		if (run.shown.empty() || run.shown.back() != number)
			run.shown.push_back(number);
	}
	return run;
}

bool around(double a, double b)
{
	return std::abs(a - b) < kStep * 1.5;
}

bool countdownTest()
{
	Tally tally;
	{ // quiet from the start: 5 s, then 5 4 3 2 1 0, then quiet again from zero
		SilenceCountdown countdown;
		const Run run = play(countdown, 0, 20, true, kNever);
		tally.expect(run.events.size() >= 2 && run.events[0].second == Event::Started && around(run.events[0].first, 5),
			     "starts after 5 s of quiet");
		tally.expect(run.events.size() >= 2 && run.events[1].second == Event::Zero && around(run.events[1].first, 10),
			     "zero 5 s later");
		tally.expect(run.events.size() == 3 && run.events[2].second == Event::Started &&
				     around(run.events[2].first, 10 + SilenceCountdown::kZeroHoldSeconds + 5),
			     "the next one needs 5 s of quiet after zero");
		tally.expect(run.shown.size() >= 8 && run.shown[0] == -1 && run.shown[1] == 5 && run.shown[2] == 4 &&
				     run.shown[3] == 3 && run.shown[4] == 2 && run.shown[5] == 1 && run.shown[6] == 0 &&
				     run.shown[7] == -1,
			     "shows nothing, 5 4 3 2 1 0, nothing");
	}
	{ // talking during the countdown stops it
		SilenceCountdown countdown;
		play(countdown, 0, 7, true, kNever);
		tally.expect(countdown.counting(), "counting at 7 s");
		const Run run = play(countdown, 7, 8, true, 7.0);
		tally.expect(!run.events.empty() && run.events[0].second == Event::Cancelled && !countdown.counting() &&
				     run.shown.back() == -1,
			     "talking stops it and the number goes");
		const Run after = play(countdown, 8, 11.9, true, 7.0);
		tally.expect(after.events.empty(), "no new one before 5 s after the talking");
		const Run again = play(countdown, 11.9, 12.5, true, 7.0);
		tally.expect(!again.events.empty() && again.events[0].second == Event::Started, "a new one 5 s after it");
	}
	{ // talking now and then keeps it from starting
		SilenceCountdown countdown;
		bool started = false;
		for (double now = 0; now < 60; now += kStep) {
			const double lastLoud = now - std::fmod(now, 4.0); // a word every 4 s
			started = started || countdown.step(now, true, lastLoud, 5) == Event::Started;
		}
		tally.expect(!started, "a word every 4 s never starts it");
	}
	{ // not armed: never; armed: the quiet counts from then
		SilenceCountdown countdown;
		const Run idle = play(countdown, 0, 30, false, kNever);
		tally.expect(idle.events.empty() && idle.shown.size() == 1 && idle.shown[0] == -1, "never while not armed");
		const Run armed = play(countdown, 30, 36, true, kNever);
		tally.expect(!armed.events.empty() && around(armed.events[0].first, 35), "5 s after arming");
		const Run off = play(countdown, 36, 37, false, kNever);
		tally.expect(!off.events.empty() && off.events[0].second == Event::Cancelled, "stops when no longer armed");
	}
	{ // other lengths of quiet
		SilenceCountdown countdown;
		const Run run = play(countdown, 0, 3, true, kNever, 2);
		tally.expect(!run.events.empty() && around(run.events[0].first, 2), "2 s of quiet");
	}
	{ // started by hand: runs unarmed, talking stops it
		SilenceCountdown countdown;
		countdown.start(100);
		const Run run = play(countdown, 100, 106, false, kNever);
		tally.expect(!run.events.empty() && run.events[0].second == Event::Zero && around(run.events[0].first, 105),
			     "by hand: zero after 5 s, unarmed");
		SilenceCountdown talked;
		talked.start(200);
		const Run stopped = play(talked, 200, 202, false, 201);
		tally.expect(!stopped.events.empty() && stopped.events[0].second == Event::Cancelled, "by hand: talking stops it");
	}
	return tally.report("countdown");
}

bool processesTest()
{
	Tally tally;
	using namespace sarxina::keeptalking;
	tally.expect(!mayClose("obs64.exe") && !mayClose("OBS64.EXE") && !mayClose("explorer.exe") && !mayClose(""),
		     "never OBS, Explorer or nothing");
	tally.expect(mayClose("Wow.exe"), "a game may be closed");

	const std::vector<Program> programs = programsWithWindows();
	bool listsObs = false;
	for (const Program &program : programs)
		listsObs = listsObs || !mayClose(program.exe);
	tally.expect(!listsObs, "the list leaves out what is never closed");

	// A program of its own: ping.exe copied under a name nothing else has.
	wchar_t system[MAX_PATH];
	GetSystemDirectoryW(system, MAX_PATH);
	const QString ping = QString::fromWCharArray(system) + QStringLiteral("\\PING.EXE");
	const QString name = QStringLiteral("keeptalking-selftest-%1.exe").arg(GetCurrentProcessId());
	const QString copy = QDir::toNativeSeparators(QDir::tempPath() + QLatin1Char('/') + name);
	QFile::remove(copy);
	const bool copied = QFile::copy(ping, copy);
	tally.expect(copied, "a program to close");
	if (copied) {
		std::wstring command = L"\"" + copy.toStdWString() + L"\" -n 60 127.0.0.1";
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};
		const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
						    nullptr, nullptr, &startup, &process);
		tally.expect(started, "it starts");
		if (started) {
			const std::string exe = name.toStdString();
			tally.expect(isRunning(exe), "it is running");
			const Closed closed = closeProgram(exe);
			tally.expect(closed.found == 1 && closed.closed == 1, "closing it ends exactly that one");
			tally.expect(WaitForSingleObject(process.hProcess, 3000) == WAIT_OBJECT_0, "it has ended");
			tally.expect(!isRunning(exe), "no longer running");
			tally.expect(closeProgram(exe).found == 0, "closing it again finds nothing");
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
		}
		QFile::remove(copy);
	}
	tally.expect(closeProgram("obs64.exe").found == 0, "asked to close OBS, it finds nothing to close");
	return tally.report("closing the game");
}

// Made-up sound: `perSecond` syllables a second for `seconds`, each 120 ms of a voice at 150 Hz (its first ten
// harmonics), or of hiss when `voiced` is false, faded in and out, with near-silence between them.
std::vector<float> syllableSound(double rate, double seconds, double perSecond, bool voiced)
{
	std::vector<float> out(static_cast<size_t>(rate * seconds));
	uint32_t noise = 12345;
	const double period = 1.0 / perSecond, on = 0.12;
	for (size_t i = 0; i < out.size(); i++) {
		const double t = i / rate;
		const double into = std::fmod(t, period);
		const double envelope = into < on ? std::sin(3.14159265358979 * into / on) : 0.0;
		double sound = 0;
		if (voiced)
			for (int h = 1; h <= 10; h++)
				sound += std::sin(2 * 3.14159265358979 * 150 * h * t) / h;
		else {
			noise = noise * 1664525u + 1013904223u;
			sound = (static_cast<double>(noise >> 8) / (1 << 24) - 0.5) * 3;
		}
		noise = noise * 1664525u + 1013904223u;
		const double room = (static_cast<double>(noise >> 8) / (1 << 24) - 0.5) * 0.0004;
		out[i] = static_cast<float>(0.3 * envelope * sound + room);
	}
	return out;
}

uint64_t count(const std::vector<float> &sound, double rate, size_t piece)
{
	sarxina::keeptalking::WordCounter counter(rate);
	for (size_t at = 0; at < sound.size(); at += piece)
		counter.add(sound.data() + at, std::min(piece, sound.size() - at));
	return counter.syllables();
}

bool wordCounterTest()
{
	Tally tally;
	const std::vector<float> voice = syllableSound(48000, 20, 4, true); // 80 syllables
	const uint64_t heard = count(voice, 48000, 1024);
	tally.expect(heard >= 76 && heard <= 84, "80 voiced syllables at 48 kHz count as 76-84 (" + std::to_string(heard) + ")");
	tally.expect(count(voice, 48000, 1) == heard && count(voice, 48000, 480) == heard,
		     "the same count whatever size the pieces of sound come in");
	const uint64_t at44 = count(syllableSound(44100, 20, 4, true), 44100, 1024);
	const uint64_t at16 = count(syllableSound(16000, 20, 4, true), 16000, 1024);
	tally.expect(at44 >= 76 && at44 <= 84 && at16 >= 76 && at16 <= 84,
		     "the same voice at 44.1 and 16 kHz too (" + std::to_string(at44) + ", " + std::to_string(at16) + ")");
	const uint64_t fast = count(syllableSound(48000, 20, 7, true), 48000, 1024);
	tally.expect(fast >= 133 && fast <= 147, "fast talk, 140 syllables, counts as 133-147 (" + std::to_string(fast) + ")");
	const uint64_t hiss = count(syllableSound(48000, 20, 4, false), 48000, 1024);
	tally.expect(hiss <= 4, "80 bursts of hiss (not a voice) count as 4 at most (" + std::to_string(hiss) + ")");
	tally.expect(count(std::vector<float>(48000 * 10, 0.f), 48000, 1024) == 0, "silence counts nothing");
	sarxina::keeptalking::WordCounter counter(48000);
	counter.add(voice.data(), voice.size());
	counter.reset();
	tally.expect(counter.syllables() == 0 && counter.seconds() == 0, "starting over clears the count");
	const double words = static_cast<double>(heard) / sarxina::keeptalking::WordCounter::kSyllablesPerWord;
	sarxina::test::report(true, "word count: 80 syllables -> %llu counted, %.0f words", static_cast<unsigned long long>(heard), words);
	return tally.report("the word count");
}

bool pictureTest(const char *folder)
{
	Tally tally;
	using namespace sarxina::keeptalking;
	for (int number = 0; number <= 5; number++) {
		const QImage picture = paintNumber(number, 1920, 1080);
		bool amber = false;
		for (int y = 0; y < picture.height() && !amber; y += 2)
			for (int x = 0; x < picture.width() && !amber; x += 2) {
				const QColor pixel = QColor::fromRgba(picture.pixel(x, y));
				amber = pixel.alpha() == 255 && pixel.red() > 240 && pixel.blue() < 90;
			}
		const QColor corner = QColor::fromRgba(picture.pixel(0, 0));
		tally.expect(picture.height() > 1080 * 0.55 && picture.height() < 1080 * 0.95 && picture.width() <= 1920 && amber && corner.alpha() == 0,
			     "the number " + std::to_string(number) + " with its line is big, amber, inside the canvas and clear around it");
		if (folder && *folder) {
			QDir().mkpath(QString::fromUtf8(folder));
			picture.save(QString::fromUtf8(folder) + QStringLiteral("/number-%1.png").arg(number));
		}
	}
	sarxina::test::report(true, "the number is drawn in %s", countdownFont().toUtf8().constData());
	return tally.report("the number");
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	const bool ok = countdownTest() & processesTest() & wordCounterTest() & pictureTest(picturesFolder);
	sarxina::test::report(ok, ok ? "passed" : "FAILED");
	return ok;
}
