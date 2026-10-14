// The physics the games share: bodies that fall, and the ways they bounce. Nothing here knows about OBS, Qt or
// buttons.
#pragma once

#include <algorithm>
#include <cmath>

namespace sarxina {

inline constexpr double kGravity = 1100.0;   // units per second, per second
inline constexpr double kStep = 1.0 / 120.0; // seconds per physics step

struct Point {
	double x = 0, y = 0;
};

struct Body {
	double x = 0, y = 0, vx = 0, vy = 0;

	Point at() const { return {x, y}; }

	// One step of falling freely.
	void fall(double dt, double gravity = kGravity)
	{
		vy += gravity * dt;
		x += vx * dt;
		y += vy * dt;
	}
};

// Keeps one coordinate of a body between two walls. At a wall it turns back with `spring` of its speed.
inline void bounceBetween(double &position, double &speed, double low, double high, double spring)
{
	if (position < low) {
		position = low;
		speed = std::abs(speed) * spring;
	}
	if (position > high) {
		position = high;
		speed = -std::abs(speed) * spring;
	}
}

// The floor under a falling body. Landing, it keeps `bounce` of its speed down, turned up, and `grip` of its speed
// sideways; a bounce slower than `rest` stops. True when the body touched the floor.
inline bool bounceOnFloor(Body &body, double floor, double bounce, double grip, double rest)
{
	if (body.y <= floor)
		return false;
	body.y = floor;
	body.vy *= -bounce;
	body.vx *= grip;
	if (std::abs(body.vy) < rest)
		body.vy = 0;
	return true;
}

// A ball against something round: pushed out of it and bounced off, keeping `spring` of the speed it came in with.
// Returns that speed, or 0 if they did not touch.
inline double bounceOff(Body &ball, double ballRadius, Point centre, double radius, double spring)
{
	const double dx = ball.x - centre.x, dy = ball.y - centre.y;
	const double d = std::hypot(dx, dy);
	const double reach = ballRadius + radius;
	if (d >= reach || d == 0)
		return 0;
	ball.x = centre.x + dx / d * reach;
	ball.y = centre.y + dy / d * reach;
	const double speed = -(ball.vx * dx + ball.vy * dy) / d;
	if (speed <= 0)
		return 0;
	ball.vx += dx / d * speed * (1 + spring);
	ball.vy += dy / d * speed * (1 + spring);
	return speed;
}

// The same against a straight edge `radius` thick, from `from` to `to`: the ball bounces off its nearest point.
inline double bounceOff(Body &ball, double ballRadius, Point from, Point to, double radius, double spring)
{
	const double ax = to.x - from.x, ay = to.y - from.y;
	const double along =
		std::clamp(((ball.x - from.x) * ax + (ball.y - from.y) * ay) / (ax * ax + ay * ay), 0.0, 1.0);
	return bounceOff(ball, ballRadius, {from.x + ax * along, from.y + ay * along}, radius, spring);
}

} // namespace sarxina
