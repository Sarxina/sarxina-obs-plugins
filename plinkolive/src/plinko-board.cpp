#include "plinko-board.hpp"

#include "panel-colors.hpp"

#include <QPainter>

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
constexpr double kSlotInset = 4.0;        // between a button and the sides and the floor of its slot
constexpr Point kStart{kWidth / 2, 48.0}; // where the head hangs before a drop, over the top peg
constexpr double kTopRowY = 76.0;
// Nine rows, the pegs in a row two thirds of a slot apart. Every third peg of the bottom row stands over a divider,
// and the other two dividers stand in the middle of a gap. The board keeps this shape on every panel; a taller panel
// only makes the slots deeper. With the pegs half a slot apart, the head (30 across) barely fits between the pegs of
// neighbouring rows: it slides down the outside of the triangle, and nearly every drop ends in an end slot.
constexpr int kRows = 9;
constexpr double kPegSpacing = kSlotWidth * 2 / 3;
constexpr double kRowGap = 32.0;
constexpr double kBottomRowY = kTopRowY + kRowGap * (kRows - 1);
constexpr double kSlotRoom = 36.0;        // from the bottom row of pegs down to the buttons: the head, and some air
constexpr double kPegRadius = 4.0;
constexpr double kDividerWidth = 2.0;
constexpr double kPegSpring = 0.5;        // the share of its speed the head keeps when it bounces
constexpr double kWallSpring = 0.5;
constexpr double kNudge = 30.0;           // the most a peg hit adds to the head's sideways speed, either way
constexpr double kNudgeSpeed = 50.0;      // anything slower is the head rolling over a peg, not hitting it
constexpr double kStillSpeed = 20.0;
constexpr double kStillSeconds = 0.25;    // balanced on a peg for this long, the head is tipped off
constexpr double kTipSpeed = 60.0;

} // namespace

std::unique_ptr<Game> makeGame()
{
	return std::make_unique<PlinkoBoard>();
}

PlinkoBoard::PlinkoBoard() : rng_(std::random_device{}()) {}

double PlinkoBoard::random()
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}

double PlinkoBoard::minHeight() const
{
	return kBottomRowY + kSlotRoom + kButtonH + kSlotInset + kBelowGround;
}

void PlinkoBoard::fit(const ControlPanel &panel)
{
	ground_ = panel.ground();
}

// Peg i of a row, counted from the left. Row 0 is the top one; row r has r + 1 pegs.
Point PlinkoBoard::peg(int row, int i)
{
	return {kStart.x + (i - row / 2.0) * kPegSpacing, kTopRowY + kRowGap * row};
}

double PlinkoBoard::slotY() const
{
	return ground_ - kSlotInset - kButtonH / 2;
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
	panel.head().x = kStart.x;
	panel.head().y = kStart.y;
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

bool PlinkoBoard::fall(Head &head, double dt)
{
	head.fly(dt);

	for (int row = 0; row < kRows; row++) {
		for (int i = 0; i <= row; i++) {
			if (bounceOff(head, kHeadRadius, peg(row, i), kPegRadius, kPegSpring) > kNudgeSpeed)
				head.vx += (random() * 2 - 1) * kNudge;
		}
	}
	// The dividers hang from the bottom row.
	for (size_t slot = 1; slot < kControlCount; slot++) {
		const double x = kSlotWidth * slot;
		bounceOff(head, kHeadRadius, {x, kBottomRowY}, {x, ground_}, kDividerWidth / 2, kWallSpring);
	}
	bounceBetween(head.x, head.vx, kHeadRadius, kWidth - kHeadRadius, kWallSpring);

	still_ = std::hypot(head.vx, head.vy) < kStillSpeed ? still_ + dt : 0;
	if (still_ > kStillSeconds) {
		head.vx = (random() < 0.5 ? -1 : 1) * kTipSpeed;
		head.vy = -kTipSpeed;
		still_ = 0;
	}

	const double floor = slotY() - kButtonH / 2 - kHeadRadius;
	if (head.y < floor)
		return false;
	head.y = floor;
	head.vx = head.vy = 0;
	return true;
}

void PlinkoBoard::step(ControlPanel &panel, double dt, double now)
{
	Head &head = panel.head();
	if (head.state != HeadState::Flying || !fall(head, dt))
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
	g.setPen(Qt::NoPen);
	g.setBrush(kIdleColor);
	for (int row = 0; row < kRows; row++) {
		for (int i = 0; i <= row; i++) {
			const Point p = peg(row, i);
			g.drawEllipse(QPointF(p.x, p.y), kPegRadius, kPegRadius);
		}
	}

	g.setPen(QPen(kIdleColor, kDividerWidth, Qt::SolidLine, Qt::RoundCap));
	for (size_t slot = 1; slot < kControlCount; slot++) {
		const double x = kSlotWidth * slot;
		g.drawLine(QPointF(x, kBottomRowY), QPointF(x, ground_ - kDividerWidth / 2));
	}
}

} // namespace sarxina
