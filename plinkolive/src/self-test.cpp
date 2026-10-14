// PlinkoLive's self-test (see self-test.hpp): the head is dropped from 31 places spread across the panel, on the
// lowest panel there is, on the usual one and on a tall one. Every drop has to come down in one slot, every slot has
// to be reached, and in practice mode nothing may reach OBS. Then pictures of the dock.

#include "self-test.hpp"

#include "controls-dock.hpp"
#include "plinko-board.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

using namespace sarxina;

namespace {

constexpr double kPanelWidth = 640;
constexpr double kHeights[] = {kMinHeight, 430, 640};
constexpr int kDrops = 31;
constexpr double kDropMargin = 19; // the head does not hang any closer to the walls
constexpr double kDropSeconds = 8; // every drop has to be down in a slot by then
constexpr uint32_t kSeed = 1;
constexpr double kPictureX = 250; // where the head is dropped from in the pictures

struct Drop {
	std::vector<Control> pressed;
	double seconds = -1; // until the head was down in a slot; negative if it never was
	int calls = 0;       // made to the host
};

// Drops the head at x and plays on until a new head is ready at the top.
Drop drop(double height, double x, bool practice)
{
	test::CountingHost host;
	PlinkoBoard board;
	board.seed(kSeed);
	ControlPanel panel(board);
	panel.setPractice(practice);
	panel.setHost(&host);
	panel.setHeight(height);
	Drop result;
	panel.setPressObserver([&result](Control control) { result.pressed.push_back(control); });

	double now = 0;
	const auto step = [&] {
		now += kStep * 1000.0;
		panel.step(kStep, now);
	};

	panel.pointerDown({x, 0}, now);
	while (panel.head().state == HeadState::Flying && now < kDropSeconds * 1000.0)
		step();
	if (panel.head().state != HeadState::Flying)
		result.seconds = now / 1000.0;
	for (int i = 0; i < 120 * 15 && (panel.head().state != HeadState::Ready || panel.poweredOff()); i++)
		step();
	result.calls = host.calls;
	return result;
}

bool drops()
{
	int passed = 0, practiceCalls = 0, realCalls = 0, realPresses = 0;
	for (const double height : kHeights) {
		int landed = 0;
		double slowest = 0;
		std::set<Control> reached;
		for (int i = 0; i < kDrops; i++) {
			const double x = kDropMargin + i * (kWidth - 2 * kDropMargin) / (kDrops - 1);
			const Drop practice = drop(height, x, true);
			practiceCalls += practice.calls;
			if (practice.seconds >= 0 && practice.pressed.size() == 1) {
				landed++;
				slowest = std::max(slowest, practice.seconds);
				reached.insert(practice.pressed[0]);
			}
			// The same drop in real mode, where the press has to reach the host. Exit asks nothing of it.
			const Drop real = drop(height, x, false);
			realCalls += real.calls;
			realPresses += real.pressed.size() == 1 && real.pressed[0] != Control::Exit;
		}

		const int reachedSlots = static_cast<int>(reached.size());
		const bool ok = landed == kDrops && reachedSlots == static_cast<int>(kControlCount);
		passed += ok;
		test::report(ok,
			     "%.0f x %.0f panel, %d drops -> %d came down in one slot each (the slowest after %.1f seconds), %d of %d slots reached: %s",
			     kPanelWidth, height, kDrops, landed, slowest, reachedSlots, static_cast<int>(kControlCount),
			     ok ? "OK" : "DIFFERENT");
	}

	const bool quiet = practiceCalls == 0 && realCalls > 0 && realCalls == realPresses;
	test::report(quiet,
		     "practice mode -> %d calls to OBS (real mode, with a stand-in for OBS: %d calls for %d presses): %s",
		     practiceCalls, realCalls, realPresses, quiet ? "OK" : "DIFFERENT");

	const int total = static_cast<int>(std::size(kHeights));
	const bool all = passed == total && quiet;
	test::report(all, "%s (%d of %d panels as expected, practice mode %s)", all ? "passed" : "FAILED", passed,
		     total, quiet ? "as expected" : "DIFFERENT");
	return all;
}

// Paints the dock, never shown, in the states worth looking at.
void pictures(const char *folder)
{
	test::Pictures pictures(folder);
	if (!pictures.wanted())
		return;

	{
		ControlsDock dock(makeGame(), ControlsDock::Use::SelfTest);
		static_cast<PlinkoBoard &>(dock.panel().game()).seed(kSeed);
		test::settle(dock);
		ControlPanel &panel = dock.panel();
		panel.pointerMove({kPictureX, 0});
		pictures.save(dock, "01-ready");

		int presses = 0;
		panel.setPressObserver([&presses](Control) { presses++; });
		panel.pointerDown({kPictureX, 0}, dock.clock());
		dock.advance(1.0);
		pictures.save(dock, "02-falling");

		for (int i = 0; i < 120 * kDropSeconds && presses == 0; i++)
			dock.advance(kStep);
		dock.advance(0.08);
		pictures.save(dock, "03-landed");
	}
	// The board again at the smallest size the dock goes to, on the lowest panel and on a tall one.
	const struct {
		int width, height;
		const char *title;
	} shapes[] = {{240, 160, "04-smallest"}, {640, 260, "05-low"}, {400, 640, "06-tall"}};
	for (const auto &shape : shapes) {
		ControlsDock dock(makeGame(), ControlsDock::Use::SelfTest);
		test::settle(dock, shape.width, shape.height);
		pictures.save(dock, shape.title);
	}
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	const bool dropped = drops();
	pictures(picturesFolder);
	return dropped;
}
