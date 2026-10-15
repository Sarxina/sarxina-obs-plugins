// PlinkoLive's self-test (see self-test.hpp): the head is dropped from the top 2000 times, each time with the peg
// nudges seeded differently. On the lowest panel there is, every drop has to come down in one slot and every slot has
// to get at least one drop in a hundred. On two taller panels, where only the slots are deeper, every drop has to
// come down in the same slot as on the lowest. In practice mode nothing may reach OBS. Then pictures of the dock.

#include "self-test.hpp"

#include "controls-dock.hpp"
#include "plinko-board.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

using namespace sarxina;

namespace {

constexpr double kPanelWidth = 640;
constexpr double kTallerHeights[] = {640, 1024};
constexpr int kDrops = 2000;
constexpr double kLeastShare = 0.01; // of the drops, for every slot
constexpr double kDropSeconds = 8;   // every drop has to be down in a slot by then
constexpr uint32_t kSeed = 1;        // for the pictures

struct Drop {
	std::vector<Control> pressed;
	double seconds = -1; // until the head was down in a slot; negative if it never was
	int calls = 0;       // made to the host
};

// Drops the head and plays on until a new head is ready at the top.
Drop drop(double height, uint32_t seed, bool practice)
{
	test::CountingHost host;
	PlinkoBoard board;
	board.seed(seed);
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

	panel.pointerDown({0, 0}, now);
	while (panel.head().state == HeadState::Flying && now < kDropSeconds * 1000.0)
		step();
	if (panel.head().state != HeadState::Flying)
		result.seconds = now / 1000.0;
	for (int i = 0; i < 120 * 15 && (panel.head().state != HeadState::Ready || panel.poweredOff()); i++)
		step();
	result.calls = host.calls;
	return result;
}

// The one slot a drop came down in, or -1.
int slot(const Drop &drop)
{
	return drop.seconds >= 0 && drop.pressed.size() == 1 ? static_cast<int>(index(drop.pressed[0])) : -1;
}

bool drops()
{
	const double lowest = PlinkoBoard().minHeight();
	std::vector<int> landedIn(kDrops);
	std::array<int, kControlCount> perSlot{};
	int landed = 0, practiceCalls = 0, realCalls = 0, realPresses = 0;
	double slowest = 0;
	for (int i = 0; i < kDrops; i++) {
		const auto seed = static_cast<uint32_t>(i + 1);
		const Drop practice = drop(lowest, seed, true);
		practiceCalls += practice.calls;
		landedIn[i] = slot(practice);
		if (landedIn[i] >= 0) {
			landed++;
			perSlot[static_cast<size_t>(landedIn[i])]++;
			slowest = std::max(slowest, practice.seconds);
		}
		// The same drop in real mode, where the press has to reach the host. Exit asks nothing of it.
		const Drop real = drop(lowest, seed, false);
		realCalls += real.calls;
		realPresses += real.pressed.size() == 1 && real.pressed[0] != Control::Exit;
	}

	std::string shares;
	bool spread = true;
	for (size_t i = 0; i < kControlCount; i++) {
		const double share = static_cast<double>(perSlot[i]) / kDrops;
		spread = spread && share >= kLeastShare;
		char part[96];
		std::snprintf(part, sizeof(part), "%s%s %.1f%%", i ? ", " : "",
			      test::names({static_cast<Control>(i)}).c_str(), share * 100);
		shares += part;
	}
	const bool lowestOk = landed == kDrops && spread;
	test::report(lowestOk,
		     "%.0f x %.0f panel, %d drops from the top -> %d came down in one slot each (the slowest after %.1f seconds): %s: %s",
		     kPanelWidth, lowest, kDrops, landed, slowest, shares.c_str(), lowestOk ? "OK" : "DIFFERENT");
	int passed = lowestOk;

	for (const double height : kTallerHeights) {
		int same = 0;
		double tallSlowest = 0;
		for (int i = 0; i < kDrops; i++) {
			const Drop tall = drop(height, static_cast<uint32_t>(i + 1), true);
			practiceCalls += tall.calls;
			if (landedIn[i] >= 0 && slot(tall) == landedIn[i]) {
				same++;
				tallSlowest = std::max(tallSlowest, tall.seconds);
			}
		}
		const bool ok = same == kDrops;
		passed += ok;
		test::report(ok,
			     "%.0f x %.0f panel, the same %d drops -> %d came down in the same slot as on the %.0f x %.0f panel (the slowest after %.1f seconds): %s",
			     kPanelWidth, height, kDrops, same, kPanelWidth, lowest, tallSlowest, ok ? "OK" : "DIFFERENT");
	}

	const bool quiet = practiceCalls == 0 && realCalls > 0 && realCalls == realPresses;
	test::report(quiet,
		     "practice mode -> %d calls to OBS (real mode, with a stand-in for OBS: %d calls for %d presses): %s",
		     practiceCalls, realCalls, realPresses, quiet ? "OK" : "DIFFERENT");

	const int total = 1 + static_cast<int>(std::size(kTallerHeights));
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
		pictures.save(dock, "01-ready");

		int presses = 0;
		panel.setPressObserver([&presses](Control) { presses++; });
		panel.pointerDown({0, 0}, dock.clock());
		dock.advance(0.8);
		pictures.save(dock, "02-falling");

		for (int i = 0; i < 120 * kDropSeconds && presses == 0; i++)
			dock.advance(kStep);
		dock.advance(0.08);
		pictures.save(dock, "03-landed");
	}
	// The board again at the smallest size the dock goes to, in a low wide dock and in a tall one.
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
