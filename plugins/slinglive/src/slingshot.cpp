#include "slingshot.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace sarxina {

namespace {

constexpr double kMaxPull = 96.0;
constexpr double kGrabReach = 46.0;       // how near the head a click picks it up
constexpr double kShortestPull = 14.0;    // let go any nearer the fork, and the head just goes back
constexpr double kLongestFlight = 3.2;    // seconds
constexpr double kDriftSpeed = 1.6;       // floating off, a button moves at this many times its drift
constexpr double kFleeReach = 120.0;      // Start Streaming runs from a cursor this near
constexpr double kFleeSpeed = 260.0;
const QColor kWoodColor(0x8a, 0x5a, 0x2b);
const QColor kBandColor(0x3b, 0x24, 0x16);

// Where each button floats.
struct Spot {
	double fx, fy;         // home, as fractions of the panel
	double phase;          // offsets the gentle bobbing, so the buttons do not move together
	double driftX, driftY; // the way it floats off in the warm-up
};

// In the order of the controls.
constexpr Spot kSpots[kControlCount] = {
	{0.66, 0.44, 0.0, -40, -12}, // Start Streaming
	{0.60, 0.20, 3.4, -58, 56},  // Start Recording
	{0.84, 0.24, 5.1, 67, -12},  // Start Virtual Camera
	{0.62, 0.68, 6.8, -76, 22},  // Studio Mode
	{0.85, 0.74, 8.5, 85, 56},   // Settings
	{0.87, 0.50, 1.7, 49, 22},   // Exit
};

const Spot &spot(Control control)
{
	return kSpots[index(control)];
}

bool held(const Head &head)
{
	return head.state == HeadState::Held || head.state == HeadState::Ready;
}

QPen bandPen()
{
	return QPen(kBandColor, 4, Qt::SolidLine, Qt::RoundCap);
}

} // namespace

std::unique_ptr<Game> makeGame()
{
	return std::make_unique<Slingshot>();
}

const char *Slingshot::hint() const
{
	return floating_ ? "Hint.FloatingButtons" : "Hint.Slingshot";
}

Point Slingshot::fork(const ControlPanel &panel)
{
	return {118.0, panel.height() - 118.0};
}

double Slingshot::power(const ControlPanel &panel)
{
	return std::sqrt(2.0 * kGravity * (panel.ground() - kHeadRadius - 4.0)) / 73.0;
}

Point Slingshot::home(const ControlPanel &panel, Control control) const
{
	const Spot &s = spot(control);
	return {kWidth * s.fx, 34.0 + (panel.ground() - 74.0) * s.fy};
}

void Slingshot::reload(ControlPanel &panel)
{
	const Point f = fork(panel);
	panel.head().x = f.x;
	panel.head().y = f.y;
}

bool Slingshot::keyPressed(ControlPanel &panel, int key, double now)
{
	if (key != Qt::Key_1 && key != Qt::Key_2)
		return false;
	floating_ = key == Qt::Key_1;
	panel.restart();
	if (floating_) {
		for (Button &b : panel.buttons()) {
			b.vx = spot(b.control).driftX * kDriftSpeed;
			b.vy = spot(b.control).driftY * kDriftSpeed;
		}
	}
	panel.showToast(floating_ ? "Toast.FloatingButtons" : "Toast.Slingshot", now);
	return true;
}

void Slingshot::moveButton(ControlPanel &panel, Button &b, double dt, double now)
{
	if (floating_) {
		drift(panel, b, dt);
		return;
	}
	// Bobbing gently in place.
	const Point h = home(panel, b.control);
	const double phase = spot(b.control).phase;
	b.x = h.x + std::sin(now / 900 + phase) * 5;
	b.y = h.y + std::sin(now / 620 + phase * 2) * 7;
}

// The warm-up: drifting around like a screensaver, and Start Streaming runs from the cursor.
void Slingshot::drift(const ControlPanel &panel, Button &b, double dt) const
{
	if (b.vx == 0 && b.vy == 0) {
		b.vx = spot(b.control).driftX * kDriftSpeed;
		b.vy = spot(b.control).driftY * kDriftSpeed;
	}
	if (b.control == Control::Stream) {
		const double dx = b.x - mouse_.x, dy = b.y - mouse_.y;
		const double d = std::hypot(dx, dy);
		if (d < kFleeReach && d > 0) {
			b.vx += dx / d * 900 * dt;
			b.vy += dy / d * 900 * dt;
		}
		const double speed = std::hypot(b.vx, b.vy);
		if (speed > kFleeSpeed) {
			b.vx *= kFleeSpeed / speed;
			b.vy *= kFleeSpeed / speed;
		}
	}
	b.x += b.vx * dt;
	b.y += b.vy * dt;
	bounceBetween(b.x, b.vx, kButtonW / 2 + 4, kWidth - kButtonW / 2 - 4, 1);
	bounceBetween(b.y, b.vy, kButtonH / 2 + 30, panel.ground() - kButtonH / 2 - 4, 1);
}

void Slingshot::pointerDown(ControlPanel &panel, Point p, double now)
{
	mouse_ = p;
	if (floating_) {
		for (Button &b : panel.buttons()) {
			if (!b.knocked && panel.touches(b, p)) {
				panel.press(b, now);
				panel.burst(b.at(), 10, 0xfefefe);
				break;
			}
		}
		return;
	}
	Head &head = panel.head();
	if (head.state == HeadState::Ready && std::hypot(p.x - head.x, p.y - head.y) < kGrabReach)
		head.state = HeadState::Held;
}

void Slingshot::pointerMove(ControlPanel &panel, Point p)
{
	mouse_ = p;
	Head &head = panel.head();
	if (head.state != HeadState::Held)
		return;
	const Point f = fork(panel);
	const double dx = p.x - f.x, dy = p.y - f.y;
	const double d = std::hypot(dx, dy);
	const double k = std::min(1.0, kMaxPull / (d > 0 ? d : 1.0));
	head.x = f.x + dx * k;
	head.y = std::min(panel.ground() - kHeadRadius, f.y + dy * k);
}

void Slingshot::pointerUp(ControlPanel &panel)
{
	Head &head = panel.head();
	if (head.state != HeadState::Held)
		return;
	const Point f = fork(panel);
	const double dx = f.x - head.x, dy = f.y - head.y;
	if (std::hypot(dx, dy) < kShortestPull) {
		panel.reload();
		return;
	}
	head.vx = dx * power(panel);
	head.vy = dy * power(panel);
	head.state = HeadState::Flying;
	head.flying = 0;
}

void Slingshot::step(ControlPanel &panel, double dt, double now)
{
	Head &head = panel.head();
	if (head.state != HeadState::Flying)
		return;
	head.fly(dt);
	for (Button &b : panel.buttons()) {
		if (b.knocked || !panel.touches(b, head.at(), kHeadRadius))
			continue;
		panel.knock(b, head.vx * 0.55, head.vy * 0.4 - 220);
		panel.hit(b, now);
		head.vx *= 0.62; // the head keeps going, so one shot can press several buttons
		head.vy = head.vy * 0.5 - 60;
	}
	bounceOnFloor(head, panel.ground() - kHeadRadius, 0.42, 0.72, 60);
	bounceBetween(head.x, head.vx, kHeadRadius, kWidth - kHeadRadius, 0.5);
	if (head.flying > kLongestFlight || (std::abs(head.vx) < 12 && head.vy == 0))
		head.rest(now);
}

bool Slingshot::showsHead(const Head &head) const
{
	return !floating_ && head.state != HeadState::Resting;
}

void Slingshot::paintBehind(QPainter &g, const ControlPanel &panel) const
{
	if (floating_)
		return;
	const Point f = fork(panel);
	const Head &head = panel.head();
	g.setPen(QPen(kWoodColor, 9, Qt::SolidLine, Qt::RoundCap));
	g.drawLine(QPointF(f.x, panel.ground()), QPointF(f.x, f.y + 34));
	g.drawLine(QPointF(f.x, f.y + 34), QPointF(f.x - 15, f.y - 6));
	g.drawLine(QPointF(f.x, f.y + 34), QPointF(f.x + 15, f.y - 6));
	// The far half of the band goes behind the head.
	g.setPen(bandPen());
	g.drawLine(QPointF(f.x + 15, f.y - 6), held(head) ? QPointF(head.x, head.y) : QPointF(f.x - 15, f.y - 6));

	if (head.state != HeadState::Held)
		return;
	// Where it will go
	double x = head.x, y = head.y;
	double vx = (f.x - head.x) * power(panel), vy = (f.y - head.y) * power(panel);
	g.setPen(Qt::NoPen);
	g.setBrush(QColor(255, 255, 255, 140));
	for (int i = 0; i < 26; i++) {
		for (int k = 0; k < 4; k++) {
			vy += kGravity * kStep;
			x += vx * kStep;
			y += vy * kStep;
		}
		if (y > panel.ground())
			break;
		const double radius = 2.2 - i * 0.05;
		g.drawEllipse(QPointF(x, y), radius, radius);
	}
}

void Slingshot::paintFront(QPainter &g, const ControlPanel &panel) const
{
	const Head &head = panel.head();
	if (floating_ || !held(head))
		return;
	// The near half of the band, in front of the head.
	const Point f = fork(panel);
	g.setPen(bandPen());
	g.drawLine(QPointF(f.x - 15, f.y - 6), QPointF(head.x, head.y));
}

} // namespace sarxina
