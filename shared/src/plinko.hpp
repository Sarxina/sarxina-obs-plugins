// The Plinko board PlinkoLive and PlinkoToggle drop things down: a triangle of pegs, one at the top and one more in
// each row below, over a row of slots split by dividers that hang below the bottom row. A ball let go over the top peg
// bounces left or right off each row, nudged a little at random, and comes down in one of the slots.
//
// Nothing here knows about OBS, Qt or what the slots are for. The board is laid out in its game's own units. The ball
// is any Body: its owner moves it one step (Body::fall), and the board then pushes it back out of what it ran into.
#pragma once

#include "physics.hpp"

#include <random>

namespace sarxina {

struct PlinkoShape {
	double width = 0;  // the walls stand at 0 and here
	int slotCount = 1; // side by side across the whole width
	Point start;       // where a ball hangs before it drops: over the top peg
	double topRowY = 0;
	int rows = 1; // row r, counted from 0 at the top, has r + 1 pegs
	double pegSpacing = 0; // between neighbouring pegs of a row
	double rowGap = 0;
	double pegRadius = 4;
	double dividerGap = 0; // from the bottom row of pegs down to the tops of the dividers
	double dividerWidth = 2;
	double ballRadius = 15;
	double pegSpring = 0.5;  // the share of its speed a ball keeps when it bounces off a peg
	double wallSpring = 0.5; // and off a divider or a wall
	double nudge = 30;       // the most a peg hit adds to the ball's sideways speed, either way
	double nudgeSpeed = 50;  // anything slower is the ball rolling over a peg, not hitting it
	double stillSpeed = 20;
	double stillSeconds = 0.25; // balanced on a peg for this long, the ball is tipped off
	double tipSpeed = 60;
};

class Plinko {
public:
	explicit Plinko(const PlinkoShape &shape) : shape_(shape) {}
	const PlinkoShape &shape() const { return shape_; }

	// The dividers reach down to `ground`, and a ball is down in its slot once its centre has come down to `floor`.
	void setFloor(double ground, double floor);
	double ground() const { return ground_; }
	double floor() const { return floor_; }

	// Peg i of a row, counted from the left.
	Point peg(int row, int i) const;
	double bottomRowY() const;
	double dividerTop() const { return bottomRowY() + shape_.dividerGap; }
	double slotWidth() const { return shape_.width / shape_.slotCount; }
	// The slot a point is over, 0 to slotCount - 1.
	int slotAt(double x) const;

	// After a ball has moved one step of `dt` seconds, pushes it back out of the pegs, the dividers and the walls.
	// `still` is how long it has hardly moved: its owner keeps it from one step to the next, 0 when the ball is let go.
	// The nudges at the pegs come from `random`. True once the ball is down in a slot, where it stops.
	bool bounce(Body &ball, double &still, double dt, std::mt19937 &random) const;

private:
	PlinkoShape shape_;
	double ground_ = 0;
	double floor_ = 0;
};

} // namespace sarxina
