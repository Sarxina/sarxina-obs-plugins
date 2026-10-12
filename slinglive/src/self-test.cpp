#include "self-test.hpp"

#include "sling-dock.hpp"
#include "sling-game.hpp"

#include <obs-module.h>

#include <QDir>
#include <QImage>
#include <QString>

#include <cmath>
#include <string>
#include <vector>

using namespace sling;

namespace {

constexpr double kPanelWidth = 640, kPanelHeight = 430;
constexpr double kRadians = 3.14159265358979323846 / 180.0;

struct Fling {
	double degrees;  // direction the head is pulled from the fork, y pointing down
	double strength; // how far it is pulled
	std::vector<ButtonId> expected;
};

// What the browser version of SlingLive does with these pulls on a 640 x 430 panel.
const std::vector<Fling> kFlings = {
	{151, 88, {ButtonId::Studio, ButtonId::Settings}},
	{103, 80, {ButtonId::Record, ButtonId::Stream, ButtonId::Studio}},
	{138, 76, {ButtonId::Stream, ButtonId::Exit}},
};

// The buttons bob gently in place, so every fling is tried with the bobbing at many different points.
constexpr int kClockOffsets = 57;
constexpr double kClockOffsetMs = 100;

const char *name(ButtonId id)
{
	switch (id) {
	case ButtonId::Stream:
		return "Start Streaming";
	case ButtonId::Exit:
		return "Exit";
	case ButtonId::Record:
		return "Start Recording";
	case ButtonId::VirtualCam:
		return "Start Virtual Camera";
	case ButtonId::Studio:
		return "Studio Mode";
	case ButtonId::Settings:
		return "Settings";
	}
	return "?";
}

std::string names(const std::vector<ButtonId> &buttons)
{
	std::string list;
	for (ButtonId id : buttons) {
		if (!list.empty())
			list += ", ";
		list += name(id);
	}
	return list.empty() ? "nothing" : list;
}

void pull(Game &game, const Fling &fling, double now)
{
	const Point fork = game.fork();
	game.pointerDown(fork.x, fork.y, now);
	game.pointerMove(fork.x + std::cos(fling.degrees * kRadians) * fling.strength,
			 fork.y + std::sin(fling.degrees * kRadians) * fling.strength);
}

// Plays one fling from start to finish and returns the buttons it pressed, in order.
std::vector<ButtonId> play(const Fling &fling, double clockMs)
{
	Game game; // practice mode, no host: nothing outside the game can be touched
	game.setHeight(kPanelHeight);
	std::vector<ButtonId> pressed;
	game.setPressObserver([&pressed](ButtonId id) { pressed.push_back(id); });

	double now = clockMs;
	const auto step = [&] {
		now += kStep * 1000.0;
		game.step(kStep, now);
	};

	for (int i = 0; i < 12; i++)
		step();
	pull(game, fling, now);
	for (int i = 0; i < 12; i++)
		step();
	game.pointerUp();
	// Until the head is back on the slingshot and the panel is back on.
	for (int i = 0; i < 120 * 15; i++) {
		step();
		if (game.shot().state == ShotState::Ready && !game.poweredOff())
			break;
	}
	return pressed;
}

bool flings()
{
	blog(LOG_INFO, "[slinglive] self-test: %.0f x %.0f panel, practice mode, %d steps a second", kPanelWidth,
	     kPanelHeight, static_cast<int>(std::lround(1.0 / kStep)));

	int passed = 0;
	for (const Fling &fling : kFlings) {
		const std::vector<ButtonId> first = play(fling, 0);
		int same = 0, asExpected = 0;
		for (int i = 0; i < kClockOffsets; i++) {
			const std::vector<ButtonId> pressed = play(fling, i * kClockOffsetMs);
			same += pressed == first;
			asExpected += pressed == fling.expected;
		}
		const bool ok = asExpected == kClockOffsets;
		passed += ok;
		blog(ok ? LOG_INFO : LOG_WARNING,
		     "[slinglive] self-test: pull %.0f degrees, strength %.0f -> %s (same at %d of %d clock offsets; expected %s: %s)",
		     fling.degrees, fling.strength, names(first).c_str(), same, kClockOffsets,
		     names(fling.expected).c_str(), ok ? "OK" : "DIFFERENT");
	}

	const int total = static_cast<int>(kFlings.size());
	blog(passed == total ? LOG_INFO : LOG_WARNING, "[slinglive] self-test: %s (%d of %d flings as expected)",
	     passed == total ? "passed" : "FAILED", passed, total);
	return passed == total;
}

// Paints the dock, never shown, in the states worth looking at.
void pictures(const QString &folder)
{
	if (!QDir().mkpath(folder)) {
		blog(LOG_WARNING, "[slinglive] self-test: cannot create %s", folder.toUtf8().constData());
		return;
	}

	int written = 0;
	const auto save = [&](SlingDock &dock, const char *title) {
		for (int ratio : {1, 2}) {
			const QString file = QDir(folder).filePath(QStringLiteral("%1@%2x.png").arg(QLatin1String(title)).arg(ratio));
			written += dock.snapshot(ratio).save(file, "PNG");
		}
	};
	const auto newDock = [](SlingDock &dock) {
		dock.setPanelSize(static_cast<int>(kPanelWidth), static_cast<int>(kPanelHeight));
		dock.advance(0.5);
	};

	{
		SlingDock dock(SlingDock::Use::SelfTest);
		newDock(dock);
		save(dock, "01-ready");

		int presses = 0;
		dock.game().setPressObserver([&presses](ButtonId) { presses++; });
		pull(dock.game(), kFlings[0], dock.clock());
		dock.advance(0.05);
		save(dock, "02-pull");

		dock.game().pointerUp();
		for (int i = 0; i < 120 * 4 && presses == 0; i++)
			dock.advance(kStep);
		dock.advance(0.08);
		save(dock, "03-hit");
	}
	{
		SlingDock dock(SlingDock::Use::SelfTest);
		newDock(dock);
		pull(dock.game(), kFlings[1], dock.clock());
		dock.advance(0.05);
		dock.game().pointerUp();
		dock.advance(7.0);
		save(dock, "04-live-and-recording");

		dock.game().useFloatingButtons(dock.clock());
		dock.advance(1.2);
		save(dock, "05-floating-buttons");
	}
	{
		SlingDock dock(SlingDock::Use::SelfTest);
		newDock(dock);
		dock.game().powerOff(dock.clock());
		double at = 0;
		const auto until = [&](double seconds, const char *title) {
			dock.advance(seconds - at);
			at = seconds;
			save(dock, title);
		};
		until(0.30, "06-power-off-falling");
		until(0.65, "07-power-off-closing");
		until(0.90, "08-power-off-line");
		until(1.15, "09-power-off-dot");
		until(3.45, "10-power-off-coming-back");
	}

	blog(LOG_INFO, "[slinglive] self-test: wrote %d pictures of the dock to %s", written, folder.toUtf8().constData());
}

} // namespace

bool selfTestRequested()
{
	return qEnvironmentVariableIntValue("SLINGLIVE_SELFTEST") != 0;
}

bool runSelfTest(const char *picturesFolder)
{
	const bool passed = flings();
	if (picturesFolder && *picturesFolder)
		pictures(QString::fromUtf8(picturesFolder));
	return passed;
}
