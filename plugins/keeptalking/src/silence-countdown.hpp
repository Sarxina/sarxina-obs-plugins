// When the countdown runs, as plain arithmetic on times in seconds, so the self-test can play it without OBS or a mic.
//
// While it is armed (on, a mic to listen to, the game running), the mic staying under the level for `quietNeeded`
// seconds starts a countdown of five seconds, shown as 5, 4, 3, 2, 1. Going over the level while it counts stops it.
// At zero the game is closed; 0 stays up a moment, and then the quiet has to start over. A countdown started by hand
// (the test route) runs whether armed or not, and talking stops it the same way.
#pragma once

namespace sarxina::keeptalking {

class SilenceCountdown {
public:
	static constexpr double kCountdownSeconds = 5.0;
	static constexpr double kZeroHoldSeconds = 1.5;

	enum class Event { None, Started, Cancelled, Zero };

	// `lastLoud` is when the mic was last over the level (any time before `now`; very negative for never).
	Event step(double now, bool armed, double lastLoud, double quietNeeded);
	// Starts a countdown now, as if the quiet had lasted long enough.
	void start(double now);

	// The number on screen: 5 to 1 while counting, 0 at zero, -1 for nothing.
	int shown(double now) const;
	bool counting() const { return state_ == State::Counting; }

private:
	enum class State { Idle, Counting, Zero };
	State state_ = State::Idle;
	bool wasArmed_ = false;
	bool forced_ = false;
	double quietFrom_ = 0; // the quiet counts from no earlier than this
	double countStart_ = 0;
	double zeroAt_ = 0;
};

} // namespace sarxina::keeptalking
