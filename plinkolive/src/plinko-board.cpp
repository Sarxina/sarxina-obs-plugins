#include "plinko-board.hpp"

#include "panel-colors.hpp"

#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace sarxina {

namespace {

constexpr double kSlotWidth = kWidth / kControlCount;
constexpr double kSlotInset = 4.0;             // between a button and the sides and the floor of its slot
constexpr double kSlotLip = 16.0;              // how far the dividers stand above the buttons
constexpr double kSlotRoom = 40.0;             // from the last row of pegs down to the buttons: the head, and some air
constexpr double kPegSpacing = kSlotWidth / 2; // a peg over every divider and over the middle of every slot
constexpr double kPegRadius = 4.0;
constexpr double kDividerWidth = 2.0;
constexpr double kHangY = 48.0;                // where the head hangs before a drop
constexpr double kHangMargin = kHeadRadius + 4.0;
constexpr double kFirstRowY = 76.0;
constexpr double kMinRowGap = 34.0;            // any closer and the head could not get from one row to the next
constexpr double kPegSpring = 0.5;             // the share of its speed the head keeps when it bounces
constexpr double kWallSpring = 0.5;
constexpr double kNudge = 30.0;                // the most a peg hit adds to the head's sideways speed, either way
constexpr double kNudgeSpeed = 50.0;           // anything slower is the head rolling over a peg, not hitting it
constexpr double kStillSpeed = 20.0;
constexpr double kStillSeconds = 0.25;         // balanced on a peg for this long, the head is tipped off
constexpr double kTipSpeed = 60.0;

} // namespace

std::unique_ptr<Game> makeGame()
{
	return std::make_unique<PlinkoBoard>();
}

PlinkoBoard::PlinkoBoard() : aim_(kWidth / 2), rng_(std::random_device{}()) {}

double PlinkoBoard::random()
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}

void PlinkoBoard::fit(const ControlPanel &panel)
{
	ground_ = panel.ground();
	const double span = slotY() - kButtonH / 2 - kSlotRoom - kFirstRowY;
	// Rows are added two at a time, so that the first and the last both have a peg over every divider.
	rows_ = 2 * std::max(1, static_cast<int>(span / (2 * kMinRowGap))) + 1;
	rowGap_ = span / (rows_ - 1);
}

int PlinkoBoard::pegs(int row) const
{
	return 2 * static_cast<int>(kControlCount) - (row % 2 ? 0 : 1);
}

double PlinkoBoard::pegX(int row, int peg) const
{
	return kPegSpacing * (peg + (row % 2 ? 0.5 : 1.0));
}

double PlinkoBoard::rowY(int row) const
{
	return kFirstRowY + rowGap_ * row;
}

double PlinkoBoard::slotY() const
{
	return ground_ - kSlotInset - kButtonH / 2;
}

double PlinkoBoard::dividerTop() const
{
	return slotY() - kButtonH / 2 - kSlotLip;
}

Point PlinkoBoard::home(const ControlPanel &, Control control) const
{
	return {kSlotWidth * (index(control) + 0.5), slotY()};
}

double PlinkoBoard::buttonWidth() const
{
	return kSlotWidth - 2 * kSlotInset;
}

void PlinkoBoard::reload(ControlPanel &panel)
{
	panel.head().x = aim_;
	panel.head().y = kHangY;
}

void PlinkoBoard::aim(Head &head, double x)
{
	aim_ = std::clamp(x, kHangMargin, kWidth - kHangMargin);
	if (head.state == HeadState::Ready)
		head.x = aim_;
}

void PlinkoBoard::pointerMove(ControlPanel &panel, Point p)
{
	aim(panel.head(), p.x);
}

void PlinkoBoard::pointerDown(ControlPanel &panel, Point p, double)
{
	Head &head = panel.head();
	aim(head, p.x);
	if (head.state != HeadState::Ready)
		return;
	head.state = HeadState::Flying;
	head.flying = 0;
	still_ = 0;
}

bool PlinkoBoard::fall(Head &head, double dt)
{
	head.fly(dt);

	for (int row = 0; row < rows_; row++) {
		const double y = rowY(row);
		for (int peg = 0; peg < pegs(row); peg++) {
			if (bounceOff(head, kHeadRadius, {pegX(row, peg), y}, kPegRadius, kPegSpring) > kNudgeSpeed)
				head.vx += (random() * 2 - 1) * kNudge;
		}
		if (row % 2 == 0)
			continue;
		// The walls reach in to the end pegs of every other row. Left straight, they would leave a gap there
		// that the head can fall into but not through.
		for (const double wall : {0.0, kWidth}) {
			const Point tip{std::abs(wall - pegX(row, 0)), y};
			bounceOff(head, kHeadRadius, {wall, y - rowGap_}, tip, 0, kWallSpring);
			bounceOff(head, kHeadRadius, tip, {wall, y + rowGap_}, 0, kWallSpring);
		}
	}
	for (size_t slot = 1; slot < kControlCount; slot++) {
		const double x = kSlotWidth * slot;
		bounceOff(head, kHeadRadius, {x, dividerTop()}, {x, ground_}, kDividerWidth / 2, kWallSpring);
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
	g.setBrush(kGroundColor);
	for (int row = 1; row < rows_; row += 2) { // where the walls reach in to the end pegs
		const double y = rowY(row), rise = y - rowY(row - 1);
		for (const double wall : {0.0, kWidth}) {
			const double tip = std::abs(wall - pegX(row, 0));
			g.drawPolygon(QPolygonF({QPointF(wall, y - rise), QPointF(tip, y), QPointF(wall, y + rise)}));
		}
	}

	g.setBrush(kIdleColor);
	for (int row = 0; row < rows_; row++) {
		for (int peg = 0; peg < pegs(row); peg++)
			g.drawEllipse(QPointF(pegX(row, peg), rowY(row)), kPegRadius, kPegRadius);
	}

	g.setPen(QPen(kIdleColor, kDividerWidth, Qt::SolidLine, Qt::RoundCap));
	for (size_t slot = 1; slot < kControlCount; slot++) {
		const double x = kSlotWidth * slot;
		g.drawLine(QPointF(x, dividerTop()), QPointF(x, ground_ - kDividerWidth / 2));
	}
}

} // namespace sarxina
