#include "plinko.hpp"

#include "sling-game.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sling {

namespace {

// The slots from left to right, in the order of OBS's own Controls panel.
constexpr ButtonId kSlots[] = {ButtonId::Stream, ButtonId::Record,   ButtonId::VirtualCam,
			       ButtonId::Studio, ButtonId::Settings, ButtonId::Exit};
constexpr double kSlotWidth = kWidth / std::size(kSlots);
constexpr double kSlotInset = 4.0;             // between a button and the sides and the floor of its slot
constexpr double kSlotLip = 16.0;              // how far the dividers stand above the buttons
constexpr double kSlotRoom = 40.0;             // from the last row of pegs down to the buttons: the head, and some air
constexpr double kPegSpacing = kSlotWidth / 2; // a peg over every divider and over the middle of every slot
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

// Pushes the head out of whatever is within `radius` of a point and bounces it off. Returns how fast it came in.
double bounce(Shot &head, double x, double y, double radius, double spring)
{
	const double dx = head.x - x, dy = head.y - y;
	const double d = std::hypot(dx, dy);
	const double reach = kHeadRadius + radius;
	if (d >= reach || d == 0)
		return 0;
	head.x = x + dx / d * reach;
	head.y = y + dy / d * reach;
	const double speed = -(head.vx * dx + head.vy * dy) / d;
	if (speed <= 0)
		return 0;
	head.vx += dx / d * speed * (1 + spring);
	head.vy += dy / d * speed * (1 + spring);
	return speed;
}

// The same for a straight edge: the head bounces off the nearest point of it.
double bounce(Shot &head, Point from, Point to, double radius, double spring)
{
	const double ax = to.x - from.x, ay = to.y - from.y;
	const double along =
		std::clamp(((head.x - from.x) * ax + (head.y - from.y) * ay) / (ax * ax + ay * ay), 0.0, 1.0);
	return bounce(head, from.x + ax * along, from.y + ay * along, radius, spring);
}

} // namespace

Plinko::Plinko() : aim_(kWidth / 2), rng_(std::random_device{}()) {}

double Plinko::random()
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
}

void Plinko::fit(double ground)
{
	ground_ = ground;
	const double span = slotY() - kButtonH / 2 - kSlotRoom - kFirstRowY;
	// Rows are added two at a time, so that the first and the last both have a peg over every divider.
	rows_ = 2 * std::max(1, static_cast<int>(span / (2 * kMinRowGap))) + 1;
	rowGap_ = span / (rows_ - 1);
}

int Plinko::pegs(int row) const
{
	return 2 * slotCount() - (row % 2 ? 0 : 1);
}

double Plinko::pegX(int row, int peg) const
{
	return kPegSpacing * (peg + (row % 2 ? 0.5 : 1.0));
}

double Plinko::rowY(int row) const
{
	return kFirstRowY + rowGap_ * row;
}

int Plinko::slotCount() const
{
	return static_cast<int>(std::size(kSlots));
}

double Plinko::slotWidth() const
{
	return kSlotWidth;
}

double Plinko::slotX(ButtonId id) const
{
	return kSlotWidth * (std::find(std::begin(kSlots), std::end(kSlots), id) - std::begin(kSlots) + 0.5);
}

double Plinko::slotY() const
{
	return ground_ - kSlotInset - kButtonH / 2;
}

double Plinko::buttonWidth() const
{
	return kSlotWidth - 2 * kSlotInset;
}

double Plinko::dividerTop() const
{
	return slotY() - kButtonH / 2 - kSlotLip;
}

void Plinko::aim(Shot &head, double x)
{
	aim_ = std::clamp(x, kHangMargin, kWidth - kHangMargin);
	if (head.state == ShotState::Ready)
		hang(head);
}

void Plinko::hang(Shot &head) const
{
	head.x = aim_;
	head.y = kHangY;
}

void Plinko::drop(Shot &head)
{
	head.state = ShotState::Fly;
	head.flying = 0;
	still_ = 0;
}

bool Plinko::fall(Shot &head, double dt)
{
	head.flying += dt;
	head.vy += kGravity * dt;
	head.x += head.vx * dt;
	head.y += head.vy * dt;
	head.rotation += head.vx * dt / kHeadRadius;

	for (int row = 0; row < rows_; row++) {
		const double y = rowY(row);
		for (int peg = 0; peg < pegs(row); peg++) {
			if (bounce(head, pegX(row, peg), y, kPegRadius, kPegSpring) > kNudgeSpeed)
				head.vx += (random() * 2 - 1) * kNudge;
		}
		if (row % 2 == 0)
			continue;
		// The walls reach in to the end pegs of every other row. Left straight, they would leave a gap there
		// that the head can fall into but not through.
		for (const double wall : {0.0, kWidth}) {
			const Point tip{std::abs(wall - pegX(row, 0)), y};
			bounce(head, {wall, y - rowGap_}, tip, 0, kWallSpring);
			bounce(head, tip, {wall, y + rowGap_}, 0, kWallSpring);
		}
	}
	for (int slot = 1; slot < slotCount(); slot++) {
		const double x = kSlotWidth * slot;
		bounce(head, {x, dividerTop()}, {x, ground_}, kDividerWidth / 2, kWallSpring);
	}
	if (head.x < kHeadRadius) {
		head.x = kHeadRadius;
		head.vx = std::abs(head.vx) * kWallSpring;
	}
	if (head.x > kWidth - kHeadRadius) {
		head.x = kWidth - kHeadRadius;
		head.vx = -std::abs(head.vx) * kWallSpring;
	}

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

} // namespace sling
