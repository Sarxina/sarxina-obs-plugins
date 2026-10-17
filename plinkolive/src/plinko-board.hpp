// PlinkoLive's game. The head hangs at the top of the panel over the point of a triangle of pegs: one peg, then two,
// then three, and so on down to the slots along the bottom. A click drops it, always from the same place. It bounces
// left or right off each row and comes down in one of the slots, one slot for each button, and that button is pressed.
// Start Streaming and Start Recording have the two middle slots, where the head lands most often. The board itself is
// the shared one (plinko.hpp).
#pragma once

#include "game.hpp"
#include "plinko.hpp"

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
	// Room for the whole triangle and the slots under it.
	double minHeight() const override;
	void fit(const ControlPanel &panel) override;
	void reload(ControlPanel &panel) override;
	void pointerDown(ControlPanel &panel, Point p, double now) override;
	void step(ControlPanel &panel, double dt, double now) override;

	void paintBehind(QPainter &g, const ControlPanel &panel) const override;
	double toastTop() const override { return 2.0; } // the head hangs where toasts usually go

private:
	double slotY() const; // the middle of the buttons

	Plinko board_;
	double still_ = 0; // seconds the falling head has hardly moved
	std::mt19937 rng_;
};

} // namespace sarxina
