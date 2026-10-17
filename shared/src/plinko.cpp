#include "plinko.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina {

namespace {

double random(std::mt19937 &generator)
{
	return std::uniform_real_distribution<double>(0.0, 1.0)(generator);
}

} // namespace

void Plinko::setFloor(double ground, double floor)
{
	ground_ = ground;
	floor_ = floor;
}

Point Plinko::peg(int row, int i) const
{
	return {shape_.start.x + (i - row / 2.0) * shape_.pegSpacing, shape_.topRowY + shape_.rowGap * row};
}

double Plinko::bottomRowY() const
{
	return shape_.topRowY + shape_.rowGap * (shape_.rows - 1);
}

int Plinko::slotAt(double x) const
{
	return std::clamp(static_cast<int>(std::floor(x / slotWidth())), 0, shape_.slotCount - 1);
}

bool Plinko::bounce(Body &ball, double &still, double dt, std::mt19937 &generator) const
{
	const PlinkoShape &s = shape_;
	for (int row = 0; row < s.rows; row++) {
		for (int i = 0; i <= row; i++) {
			if (bounceOff(ball, s.ballRadius, peg(row, i), s.pegRadius, s.pegSpring) > s.nudgeSpeed)
				ball.vx += (random(generator) * 2 - 1) * s.nudge;
		}
	}
	const double top = dividerTop();
	for (int slot = 1; slot < s.slotCount; slot++) {
		const double x = slotWidth() * slot;
		bounceOff(ball, s.ballRadius, {x, top}, {x, ground_}, s.dividerWidth / 2, s.wallSpring);
	}
	bounceBetween(ball.x, ball.vx, s.ballRadius, s.width - s.ballRadius, s.wallSpring);

	still = std::hypot(ball.vx, ball.vy) < s.stillSpeed ? still + dt : 0;
	if (still > s.stillSeconds) {
		ball.vx = (random(generator) < 0.5 ? -1 : 1) * s.tipSpeed;
		ball.vy = -s.tipSpeed;
		still = 0;
	}

	if (ball.y < floor_)
		return false;
	ball.y = floor_;
	ball.vx = ball.vy = 0;
	return true;
}

} // namespace sarxina
