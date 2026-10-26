#include "plinko-board.hpp"

#include "panel-colors.hpp"
#include "plinko-painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace sarxina {

namespace {

constexpr double kSlotWidth = kWidth / kControlCount;
// The buttons from left to right. The head comes down in the middle two slots most often.
constexpr std::array<Control, kControlCount> kSlots = {
	Control::VirtualCam, Control::Studio, Control::Stream, Control::Record, Control::Settings, Control::Exit,
};
constexpr double kSlotInset = 4.0; // between a button and the sides and the floor of its slot
constexpr double kSlotRoom = 36.0; // from the bottom row of pegs down to the buttons: the head, and some air

// Nine rows, the pegs in a row two thirds of a slot apart. Every third peg of the bottom row stands over a divider,
// and the other two dividers stand in the middle of a gap. The board keeps this shape on every panel; a taller panel
// only makes the slots deeper. With the pegs half a slot apart, the head (30 across) barely fits between the pegs of
// neighbouring rows: it slides down the outside of the triangle, and nearly every drop ends in an end slot.
PlinkoShape shape()
{
	PlinkoShape shape;
	shape.width = kWidth;
	shape.slotCount = static_cast<int>(kControlCount);
	shape.start = {kWidth / 2, 48.0}; // where the head hangs before a drop, over the top peg
	shape.topRowY = 76.0;
	shape.rows = 9;
	shape.pegSpacing = kSlotWidth * 2 / 3;
	shape.rowGap = 32.0;
	shape.ballRadius = kHeadRadius;
	return shape;
}

} // namespace

std::unique_ptr<Game> makeGame()
{
	return std::make_unique<PlinkoBoard>();
}

PlinkoBoard::PlinkoBoard() : board_(shape()), rng_(std::random_device{}()) {}

double PlinkoBoard::minHeight() const
{
	return board_.bottomRowY() + kSlotRoom + kButtonH + kSlotInset + kBelowGround;
}

// The buttons stand on the floor of their slots, and the head comes down on top of them.
void PlinkoBoard::fit(const ControlPanel &panel)
{
	const double ground = panel.ground();
	const double buttons = ground - kSlotInset - kButtonH / 2;
	board_.setFloor(ground, buttons - kButtonH / 2 - kHeadRadius);
}

double PlinkoBoard::slotY() const
{
	return board_.ground() - kSlotInset - kButtonH / 2;
}

Point PlinkoBoard::home(const ControlPanel &, Control control) const
{
	const auto slot = std::find(kSlots.begin(), kSlots.end(), control) - kSlots.begin();
	return {kSlotWidth * (static_cast<double>(slot) + 0.5), slotY()};
}

double PlinkoBoard::buttonWidth() const
{
	return kSlotWidth - 2 * kSlotInset;
}

void PlinkoBoard::reload(ControlPanel &panel)
{
	panel.head().x = board_.shape().start.x;
	panel.head().y = board_.shape().start.y;
}

// Wherever the click is, the head drops from the start.
void PlinkoBoard::pointerDown(ControlPanel &panel, Point, double)
{
	Head &head = panel.head();
	if (head.state != HeadState::Ready)
		return;
	head.state = HeadState::Flying;
	head.flying = 0;
	still_ = 0;
}

void PlinkoBoard::step(ControlPanel &panel, double dt, double now)
{
	Head &head = panel.head();
	if (head.state != HeadState::Flying)
		return;
	head.fly(dt);
	if (!board_.bounce(head, still_, dt, rng_))
		return;
	// Down in a slot: the button in it is pressed.
	head.rest(now);
	for (Button &b : panel.buttons()) {
		if (!b.knocked && std::abs(b.x - head.x) < kSlotWidth / 2) {
			panel.hit(b, now);
			break;
		}
	}
}

void PlinkoBoard::paintBehind(QPainter &g, const ControlPanel &) const
{
	paintPlinko(g, board_, kIdleColor);
}

} // namespace sarxina
