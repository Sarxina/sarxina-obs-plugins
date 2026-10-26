// SlingLive's self-test (see self-test.hpp): three scripted flings, each tried with the buttons' bobbing at 57
// different points, have to press what the browser version of SlingLive pressed. Then pictures of the dock.

#include "self-test.hpp"

#include "controls-dock.hpp"
#include "slingshot.hpp"

#include <cmath>
#include <vector>

using namespace sarxina;

namespace {

constexpr double kPanelWidth = 640, kPanelHeight = 430;
constexpr double kRadians = 3.14159265358979323846 / 180.0;

struct Fling {
	double degrees;  // direction the head is pulled from the fork, y pointing down
	double strength; // how far it is pulled
	std::vector<Control> expected;
};

// What the browser version of SlingLive does with these pulls on a 640 x 430 panel.
const std::vector<Fling> kFlings = {
	{151, 88, {Control::Studio, Control::Settings}},
	{103, 80, {Control::Record, Control::Stream, Control::Studio}},
	{138, 76, {Control::Stream, Control::Exit}},
};

// The buttons bob gently in place, so every fling is tried with the bobbing at many different points.
constexpr int kClockOffsets = 57;
constexpr double kClockOffsetMs = 100;

void pull(ControlPanel &panel, const Fling &fling, double now)
{
	const Point fork = Slingshot::fork(panel);
	panel.pointerDown(fork, now);
	panel.pointerMove({fork.x + std::cos(fling.degrees * kRadians) * fling.strength,
			   fork.y + std::sin(fling.degrees * kRadians) * fling.strength});
}

// Plays one fling from start to finish and returns the buttons it pressed, in order.
std::vector<Control> play(const Fling &fling, double clockMs)
{
	Slingshot slingshot;
	ControlPanel panel(slingshot); // practice mode, no host: nothing outside the panel can be touched
	panel.setHeight(kPanelHeight);
	std::vector<Control> pressed;
	panel.setPressObserver([&pressed](Control control) { pressed.push_back(control); });

	double now = clockMs;
	const auto step = [&] {
		now += kStep * 1000.0;
		panel.step(kStep, now);
	};

	for (int i = 0; i < 12; i++)
		step();
	pull(panel, fling, now);
	for (int i = 0; i < 12; i++)
		step();
	panel.pointerUp();
	// Until the head is back on the slingshot and the panel is back on.
	for (int i = 0; i < 120 * 15; i++) {
		step();
		if (panel.head().state == HeadState::Ready && !panel.poweredOff())
			break;
	}
	return pressed;
}

bool flings()
{
	test::report(true, "%.0f x %.0f panel, practice mode, %d steps a second", kPanelWidth, kPanelHeight,
		     static_cast<int>(std::lround(1.0 / kStep)));

	int passed = 0;
	for (const Fling &fling : kFlings) {
		const std::vector<Control> first = play(fling, 0);
		int same = 0, asExpected = 0;
		for (int i = 0; i < kClockOffsets; i++) {
			const std::vector<Control> pressed = play(fling, i * kClockOffsetMs);
			same += pressed == first;
			asExpected += pressed == fling.expected;
		}
		const bool ok = asExpected == kClockOffsets;
		passed += ok;
		test::report(ok, "pull %.0f degrees, strength %.0f -> %s (same at %d of %d clock offsets; expected %s: %s)",
			     fling.degrees, fling.strength, test::names(first).c_str(), same, kClockOffsets,
			     test::names(fling.expected).c_str(), ok ? "OK" : "DIFFERENT");
	}

	const int total = static_cast<int>(kFlings.size());
	test::report(passed == total, "%s (%d of %d flings as expected)", passed == total ? "passed" : "FAILED", passed,
		     total);
	return passed == total;
}

// Paints the dock, never shown, in the states worth looking at.
void pictures(const char *folder)
{
	test::Pictures pictures(folder);
	if (!pictures.wanted())
		return;

	{
		ControlsDock dock(makeGame(), ControlsDock::Use::SelfTest);
		test::settle(dock);
		pictures.save(dock, "01-ready");

		int presses = 0;
		dock.panel().setPressObserver([&presses](Control) { presses++; });
		pull(dock.panel(), kFlings[0], dock.clock());
		dock.advance(0.05);
		pictures.save(dock, "02-pull");

		dock.panel().pointerUp();
		for (int i = 0; i < 120 * 4 && presses == 0; i++)
			dock.advance(kStep);
		dock.advance(0.08);
		pictures.save(dock, "03-hit");
	}
	{
		ControlsDock dock(makeGame(), ControlsDock::Use::SelfTest);
		test::settle(dock);
		pull(dock.panel(), kFlings[1], dock.clock());
		dock.advance(0.05);
		dock.panel().pointerUp();
		dock.advance(7.0);
		pictures.save(dock, "04-live-and-recording");

		dock.panel().game().keyPressed(dock.panel(), Qt::Key_1, dock.clock());
		dock.advance(1.2);
		pictures.save(dock, "05-floating-buttons");
	}
	{
		ControlsDock dock(makeGame(), ControlsDock::Use::SelfTest);
		test::settle(dock);
		dock.panel().powerOff(dock.clock());
		double at = 0;
		const auto until = [&](double seconds, const char *title) {
			dock.advance(seconds - at);
			at = seconds;
			pictures.save(dock, title);
		};
		until(0.30, "06-power-off-falling");
		until(0.65, "07-power-off-closing");
		until(0.90, "08-power-off-line");
		until(1.15, "09-power-off-dot");
		until(3.45, "10-power-off-coming-back");
	}
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	const bool flung = flings();
	pictures(picturesFolder);
	return flung;
}
