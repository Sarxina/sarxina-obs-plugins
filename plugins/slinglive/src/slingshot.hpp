// SlingLive's game. The buttons float in a loose cluster on the right, Start Streaming in the middle with Exit right
// behind it, so a clean hit on Start Streaming carries on into Exit. The head sits in a slingshot on the left: pull it
// back and let go, and whatever it hits is pressed.
//
// Key 1 is the warm-up: the buttons float off around the panel and are pressed with a click, and Start Streaming runs
// from the cursor. Key 2 brings the slingshot back.
#pragma once

#include "game.hpp"

namespace sarxina {

class Slingshot : public Game {
public:
	const char *hint() const override;
	Point home(const ControlPanel &panel, Control control) const override;
	void reload(ControlPanel &panel) override;
	void moveButton(ControlPanel &panel, Button &button, double dt, double now) override;
	void pointerDown(ControlPanel &panel, Point p, double now) override;
	void pointerMove(ControlPanel &panel, Point p) override;
	void pointerUp(ControlPanel &panel) override;
	bool keyPressed(ControlPanel &panel, int key, double now) override;
	void step(ControlPanel &panel, double dt, double now) override;

	void paintBehind(QPainter &g, const ControlPanel &panel) const override;
	bool showsHead(const Head &head) const override;
	void paintFront(QPainter &g, const ControlPanel &panel) const override;

	// Where the band is tied.
	static Point fork(const ControlPanel &panel);
	// Launch speed per unit of pull: a full pull straight down just reaches the top of the panel, however tall it is.
	static double power(const ControlPanel &panel);

private:
	void drift(const ControlPanel &panel, Button &button, double dt) const;

	bool floating_ = false; // the warm-up, key 1
	Point mouse_;
};

} // namespace sarxina
