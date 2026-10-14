// PlinkoLive's game. The head hangs at the top of the panel and follows the pointer sideways; a click drops it. It
// bounces down through staggered rows of pegs and comes down in one of the slots along the bottom, one slot for each
// button in the order of OBS's own Controls panel, and that button is pressed.
#pragma once

#include "game.hpp"

#include <cstdint>
#include <random>

namespace sarxina {

class PlinkoBoard : public Game {
public:
	PlinkoBoard();

	// A peg hit nudges the head a little at random. With the same seed, the same drops fall the same way.
	void seed(uint32_t seed) { rng_.seed(seed); }

	const char *hint() const override { return "Hint.Plinko"; }
	Point home(const ControlPanel &panel, Control control) const override;
	double buttonWidth() const override;
	// The taller the panel, the more rows of pegs.
	void fit(const ControlPanel &panel) override;
	void reload(ControlPanel &panel) override;
	void pointerDown(ControlPanel &panel, Point p, double now) override;
	void pointerMove(ControlPanel &panel, Point p) override;
	void step(ControlPanel &panel, double dt, double now) override;

	void paintBehind(QPainter &g, const ControlPanel &panel) const override;
	double toastTop() const override { return 2.0; } // the head hangs where toasts usually go

private:
	int pegs(int row) const;
	double pegX(int row, int peg) const;
	double rowY(int row) const;
	double slotY() const; // the middle of the buttons
	double dividerTop() const;

	void aim(Head &head, double x);
	// Moves a falling head one step. True once it has come down in a slot.
	bool fall(Head &head, double dt);
	double random();

	double ground_ = 0;
	int rows_ = 0;
	double rowGap_ = 0;
	double aim_;
	double still_ = 0; // seconds the falling head has hardly moved
	std::mt19937 rng_;
};

} // namespace sarxina
