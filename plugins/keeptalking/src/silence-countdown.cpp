#include "silence-countdown.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina::keeptalking {

SilenceCountdown::Event SilenceCountdown::step(double now, bool armed, double lastLoud, double quietNeeded)
{
	switch (state_) {
	case State::Zero:
		if (now - zeroAt_ >= kZeroHoldSeconds) {
			state_ = State::Idle;
			quietFrom_ = now;
		}
		return Event::None;

	case State::Counting:
		if (lastLoud >= countStart_ || (!forced_ && !armed)) {
			state_ = State::Idle;
			quietFrom_ = now;
			wasArmed_ = armed;
			return Event::Cancelled;
		}
		if (now - countStart_ >= kCountdownSeconds) {
			state_ = State::Zero;
			zeroAt_ = now;
			return Event::Zero;
		}
		return Event::None;

	case State::Idle:
		break;
	}

	if (!armed) {
		wasArmed_ = false;
		return Event::None;
	}
	if (!wasArmed_) { // the quiet counts from the moment it is armed
		wasArmed_ = true;
		quietFrom_ = now;
	}
	if (now - std::max(lastLoud, quietFrom_) >= quietNeeded) {
		state_ = State::Counting;
		forced_ = false;
		countStart_ = now;
		return Event::Started;
	}
	return Event::None;
}

void SilenceCountdown::start(double now)
{
	if (state_ == State::Counting)
		return;
	state_ = State::Counting;
	forced_ = true;
	countStart_ = now;
}

int SilenceCountdown::shown(double now) const
{
	switch (state_) {
	case State::Counting: {
		const double left = kCountdownSeconds - (now - countStart_);
		return std::clamp(static_cast<int>(std::ceil(left)), 1, static_cast<int>(kCountdownSeconds));
	}
	case State::Zero:
		return 0;
	default:
		return -1;
	}
}

} // namespace sarxina::keeptalking
