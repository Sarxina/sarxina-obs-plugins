// SlingLive: the game itself. No OBS and no Qt in here: the dock feeds it time and the pointer and draws what it
// holds, and a self-test can run it with nothing on screen.
//
// Everything is laid out in a logical space 640 units wide; the height follows the shape of the dock.
#pragma once

#include "plinko.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace sling {

inline constexpr double kWidth = 640.0;
inline constexpr double kMinHeight = 260.0;
inline constexpr double kButtonW = 138.0;
inline constexpr double kButtonH = 28.0;
inline constexpr double kHeadRadius = 15.0;
inline constexpr double kGravity = 1100.0;
inline constexpr double kMaxPull = 96.0;
inline constexpr double kStep = 1.0 / 120.0; // seconds per physics step

enum class ButtonId { Stream, Exit, Record, VirtualCam, Studio, Settings };
enum class ButtonState { Floating, Knocked };
enum class PlayMode { Slingshot, FloatingButtons, Plinko };
enum class ShotState { Ready, Drag, Fly, Done };

struct Point {
	double x = 0, y = 0;
};

struct Button {
	ButtonId id;
	double fx, fy;            // home position, as fractions of the panel
	double x = 0, y = 0;      // centre
	double vx = 0, vy = 0;
	double angle = 0, spin = 0;
	ButtonState state = ButtonState::Floating;
	double knockedFor = 0;    // seconds since it was knocked over
	double phase = 0;         // offsets the gentle bobbing so the buttons do not move together
	double flash = 0;         // 1 right after a press, fading to 0
	double driftX = 0, driftY = 0; // speed in floating-buttons mode
};

struct Shot {
	double x = 0, y = 0, vx = 0, vy = 0;
	ShotState state = ShotState::Ready;
	double flying = 0; // seconds in the air
	double rotation = 0;
	double doneAt = 0; // clock time it came to rest
};

struct Particle {
	double x, y, vx, vy;
	double age, life;
	uint32_t rgb;
};

// What the panel shows. In practice mode the game changes it itself; in real mode the dock copies it from OBS.
struct Panel {
	bool streaming = false;  // the stream button reads "Stop Streaming"
	bool connecting = false; // streaming was asked for and OBS is still connecting
	bool recording = false;
	bool virtualCam = false;
	bool studioMode = false;
	double liveSince = 0; // clock time, milliseconds
	double recordingSince = 0;
};

// Where presses go in real mode.
class Host {
public:
	virtual ~Host() = default;
	virtual void setStreaming(bool on) = 0;
	virtual void setRecording(bool on) = 0;
	virtual void setVirtualCam(bool on) = 0;
	virtual void setStudioMode(bool on) = 0;
	virtual void openSettings() = 0;
};

class Game {
public:
	Game();

	// Practice: presses only change the panel. Real: presses go to the host.
	void setPractice(bool practice);
	bool practice() const { return practice_; }
	void setHost(Host *host) { host_ = host; }
	// Called with every button a hit or a click presses, in order.
	void setPressObserver(std::function<void(ButtonId)> observer) { observer_ = std::move(observer); }

	// Height of the panel in logical units (the width is always kWidth).
	void setHeight(double height);
	double height() const { return height_; }
	double ground() const { return height_ - 30.0; }
	Point fork() const { return {118.0, height_ - 118.0}; }
	Point home(const Button &button) const;
	// How wide the buttons are: narrower in Plinko, where each one sits in a slot.
	double buttonWidth() const;
	// Launch speed per unit of pull: a full pull straight down just reaches the top of the panel, however tall it is.
	double power() const;

	// All times are the caller's clock in milliseconds.
	void step(double dt, double now);
	void pointerDown(double x, double y, double now);
	void pointerMove(double x, double y);
	void pointerUp();

	void setMode(PlayMode mode);         // without a toast, for the play style the dock remembers
	void use(PlayMode mode, double now); // keys 1 (floating buttons), 2 (slingshot) and 3 (Plinko)
	void reset(double now);              // key R
	void powerOff(double now);           // key X, and a hit on Exit

	static bool overGear(double x, double y);

	// For drawing
	PlayMode mode() const { return mode_; }
	Plinko &plinko() { return plinko_; }
	const Plinko &plinko() const { return plinko_; }
	const std::array<Button, 6> &buttons() const { return buttons_; }
	const Shot &shot() const { return shot_; }
	const std::vector<Particle> &particles() const { return particles_; }
	Panel &panel() { return panel_; }
	const Panel &panel() const { return panel_; }
	static const char *label(ButtonId id, bool on); // locale key
	const char *label(const Button &button) const { return label(button.id, isOn(button)); }
	bool isOn(const Button &button) const;
	const char *toast() const { return toast_; } // locale key, or nullptr
	double toastAt() const { return toastAt_; }
	double shake() const { return shake_; }
	bool poweredOff() const { return off_; }
	double poweredOffAt() const { return offAt_; }
	double random(); // 0..1, for the dock's own cosmetic jitter

private:
	void place();
	void reload();
	void press(Button &button, double now);
	void hit(Button &button, double now);
	void land(double now);
	void knock(Button &button, double vx, double vy);
	void burst(double x, double y, int count, uint32_t rgb);
	void showToast(const char *key, double now);

	std::array<Button, 6> buttons_;
	Shot shot_;
	Plinko plinko_;
	std::vector<Particle> particles_;
	Panel panel_;
	PlayMode mode_ = PlayMode::Slingshot;
	Point mouse_;
	double height_ = 360.0;
	double shake_ = 0;
	bool off_ = false;
	double offAt_ = 0;
	const char *toast_ = nullptr;
	double toastAt_ = 0;
	bool practice_ = true;
	Host *host_ = nullptr;
	std::function<void(ButtonId)> observer_;
	std::mt19937 rng_;
};

} // namespace sling
