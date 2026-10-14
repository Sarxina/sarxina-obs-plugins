// SlingLive's Plinko board. The head is dropped from the top, bounces down through staggered rows of pegs and comes
// down in one of the slots along the bottom, one slot for each button. Like the game it belongs to, no OBS and no Qt
// in here.
#pragma once

#include <cstdint>
#include <random>

namespace sling {

enum class ButtonId;
struct Shot;

inline constexpr double kPegRadius = 4.0;
inline constexpr double kDividerWidth = 2.0;

class Plinko {
public:
	Plinko();

	// Lays the board out between the top of the panel and the ground: the taller the panel, the more rows of pegs.
	void fit(double ground);
	// A peg hit nudges the head a little at random. With the same seed, the same drops fall the same way.
	void seed(uint32_t seed) { rng_.seed(seed); }

	// The head hangs at the top and follows the pointer sideways until it is dropped.
	void aim(Shot &head, double x);
	void hang(Shot &head) const;
	void drop(Shot &head);
	// Moves a falling head one step. True once it has come down in a slot.
	bool fall(Shot &head, double dt);

	// For drawing, and for the buttons, which sit in the slots
	int rows() const { return rows_; }
	int pegs(int row) const;
	double pegX(int row, int peg) const;
	double rowY(int row) const;
	int slotCount() const;
	double slotWidth() const;
	double slotX(ButtonId id) const; // the middle of that button's slot
	double slotY() const;            // the middle of the buttons
	double buttonWidth() const;
	double dividerTop() const;
	double ground() const { return ground_; }

private:
	double random();

	double ground_ = 0;
	int rows_ = 0;
	double rowGap_ = 0;
	double aim_;
	double still_ = 0; // seconds the falling head has hardly moved
	std::mt19937 rng_;
};

} // namespace sling
