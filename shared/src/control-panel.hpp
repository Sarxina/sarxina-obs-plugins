// The panel each plugin's game plays on: the buttons as things that can be pressed, hit and knocked over, the head,
// the sparks, and the power-off gag. How the head gets to the buttons is the game's business (game.hpp).
//
// No OBS and no Qt in here: the dock feeds the panel time and the pointer and draws what it holds, and a self-test
// can run it with nothing on screen. Everything is laid out in a space 640 units wide; the height follows the shape
// of the dock.
#pragma once

#include "controls.hpp"
#include "physics.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace sarxina {

class Game;

inline constexpr double kWidth = 640.0;
inline constexpr double kMinHeight = 260.0;  // unless the game needs more (Game::minHeight)
inline constexpr double kBelowGround = 30.0; // the floor's edge and the status strip
inline constexpr double kButtonW = 138.0;
inline constexpr double kButtonH = 28.0;
inline constexpr double kHeadRadius = 15.0;

struct Button : Body {
	Control control = Control::Stream;
	double angle = 0, spin = 0;
	bool knocked = false;  // knocked over; it floats back home after a while
	double knockedFor = 0; // seconds
	double flash = 0;      // 1 right after a press, fading to 0
};

enum class HeadState {
	Ready,   // waiting for the player
	Held,    // being pulled back
	Flying,
	Resting, // came to rest; a new head is ready a moment later
};

struct Head : Body {
	HeadState state = HeadState::Ready;
	double flying = 0; // seconds in the air
	double rotation = 0;
	double restingSince = 0; // clock time, milliseconds

	// One step through the air: it falls, and rolls as it goes.
	void fly(double dt)
	{
		flying += dt;
		fall(dt);
		rotation += vx * dt / kHeadRadius;
	}
	void rest(double now)
	{
		state = HeadState::Resting;
		restingSince = now;
	}
};

struct Particle : Body {
	double age = 0, life = 0;
	uint32_t rgb = 0;
};

class ControlPanel {
public:
	explicit ControlPanel(Game &game);
	Game &game() const { return *game_; }

	// Practice: presses only change the panel. Real: presses go to the host.
	void setPractice(bool practice);
	bool practice() const { return practice_; }
	void setHost(Host *host) { host_ = host; }
	// Called with every button that is pressed, in order.
	void setPressObserver(std::function<void(Control)> observer) { observer_ = std::move(observer); }

	void setHeight(double height);
	double height() const { return height_; }
	double minHeight() const;
	double ground() const { return height_ - kBelowGround; }
	double buttonWidth() const;

	// All times are the caller's clock in milliseconds.
	void step(double dt, double now);
	void pointerDown(Point p, double now);
	void pointerMove(Point p);
	void pointerUp();
	void reset(double now);    // key R
	void powerOff(double now); // key X, and a press of Exit
	void showToast(const char *key, double now);

	// For the game
	std::array<Button, kControlCount> &buttons() { return buttons_; }
	Head &head() { return head_; }
	void restart(); // every button back home and a new head
	void reload();  // a new head, wherever the game keeps it
	bool touches(const Button &button, Point p, double pad = 0) const;
	void press(Button &button, double now);
	void hit(Button &button, double now); // the head coming down on a button: a press, with sparks and a shake
	void knock(Button &button, double vx, double vy);
	void burst(Point at, int count, uint32_t rgb);

	// For drawing
	const std::array<Button, kControlCount> &buttons() const { return buttons_; }
	const Head &head() const { return head_; }
	const std::vector<Particle> &particles() const { return particles_; }
	PanelState &state() { return state_; }
	const PanelState &state() const { return state_; }
	bool isOn(const Button &button) const { return state_.isOn(button.control); }
	const char *label(const Button &button) const; // locale key
	const char *toast() const { return toast_; }   // locale key, or nullptr
	double toastAt() const { return toastAt_; }
	double shake() const { return shake_; }
	bool poweredOff() const { return off_; }
	double poweredOffAt() const { return offAt_; }

private:
	void place();
	void stepKnocked(Button &button, double dt);
	double random(); // 0..1, for looks only

	Game *game_;
	std::array<Button, kControlCount> buttons_;
	Head head_;
	std::vector<Particle> particles_;
	PanelState state_;
	double height_ = 360.0;
	double shake_ = 0;
	bool off_ = false;
	double offAt_ = 0;
	const char *toast_ = nullptr;
	double toastAt_ = 0;
	bool practice_ = true;
	Host *host_ = nullptr;
	std::function<void(Control)> observer_;
	std::mt19937 rng_;
};

} // namespace sarxina
