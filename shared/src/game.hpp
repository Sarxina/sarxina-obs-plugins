// What makes each plugin its own: how the head gets to the buttons (a slingshot, a Plinko board). The shared
// panel (control-panel.hpp) does everything else: what a press does, knocked-over buttons, sparks, the power-off
// gag. A plugin is the shared dock with its one game in it, given by makeGame().
#pragma once

#include "control-panel.hpp"

#include <memory>

class QPainter;

namespace sarxina {

class Game {
public:
	virtual ~Game() = default;

	// Locale key for the hint along the bottom of the dock.
	virtual const char *hint() const = 0;

	// Where each button stands, and how wide the buttons are.
	virtual Point home(const ControlPanel &panel, Control control) const = 0;
	virtual double buttonWidth() const { return kButtonW; }
	// The shortest panel the game fits on. A dock too wide for it shows the whole panel in its middle.
	virtual double minHeight() const { return kMinHeight; }
	// The panel has changed height.
	virtual void fit(const ControlPanel &) {}

	// Puts a new head where the game keeps it.
	virtual void reload(ControlPanel &) {}
	// Moves a button that is standing, not knocked over. It stays at home unless the game moves it.
	virtual void moveButton(ControlPanel &panel, Button &button, double dt, double now);
	virtual void pointerDown(ControlPanel &, Point, double) {}
	virtual void pointerMove(ControlPanel &, Point) {}
	virtual void pointerUp(ControlPanel &) {}
	// A key the dock has no use for (it handles R and X itself), as a Qt::Key. True if the game used it.
	virtual bool keyPressed(ControlPanel &, int, double) { return false; }
	// Moves the head one step, and hits whatever it reaches.
	virtual void step(ControlPanel &, double, double) {}

	// Drawing, in panel units: what goes behind the buttons, then the head if it shows, then what goes in front.
	virtual void paintBehind(QPainter &, const ControlPanel &) const {}
	virtual bool showsHead(const Head &) const { return true; }
	virtual void paintFront(QPainter &, const ControlPanel &) const {}
	// Where toasts go, clear of the head.
	virtual double toastTop() const { return 28.0; }
};

inline void Game::moveButton(ControlPanel &panel, Button &button, double, double)
{
	const Point h = home(panel, button.control);
	button.x = h.x;
	button.y = h.y;
}

// Each plugin defines this.
std::unique_ptr<Game> makeGame();

} // namespace sarxina
