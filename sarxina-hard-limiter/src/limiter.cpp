#include "limiter.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina::limiter {

Processor::Processor(double sampleRate) : rate_(sampleRate)
{
	size_ = static_cast<size_t>(std::ceil(maxLookaheadMs * sampleRate / 1000)) + 1;
	for (auto &line : line_)
		line.assign(size_, 0.0f);
	for (GainPath &p : path_)
		p.reset(size_, delay_);
	setSettings(Settings());
}

void Processor::GainPath::reset(size_t size, int delay)
{
	held.assign(size + 1, 1.0);
	heldAt.assign(size + 1, 0);
	averaged.reserve(size); // so a new look-ahead never allocates on the audio thread
	averaged.assign(static_cast<size_t>(delay) + 1, 1.0); // nothing needed yet: a gain of 1
	sum = static_cast<double>(averaged.size());
	front = back = 0;
	n = 0;
	gainDb = 0;
}

void Processor::setSettings(const Settings &settings)
{
	const int before = delay_;
	s_ = settings;
	ceiling_ = std::pow(10, s_.maximumAmplitudeDb / 20);
	boost_ = std::pow(10, s_.inputBoostDb / 20);
	releaseStepDb_ = 12 / (std::max(s_.releaseMs, 0.01) * rate_ / 1000);
	delay_ = static_cast<int>(std::lround(std::clamp(s_.lookaheadMs, 0.0, maxLookaheadMs) * rate_ / 1000));
	// A new look-ahead changes the windows' lengths: start them again (the gain itself carries on).
	if (delay_ != before)
		for (GainPath &p : path_) {
			const double gain = p.gainDb;
			p.reset(size_, delay_);
			p.gainDb = gain;
		}
}

double Processor::GainPath::next(double need, int delay, double releaseStepDb)
{
	const size_t ring = held.size();
	// The lowest need over the last delay+1 samples: drop candidates that are no lower than the new one, or too old.
	while (front != back && held[(back + ring - 1) % ring] >= need)
		back = (back + ring - 1) % ring;
	held[back] = need;
	heldAt[back] = n;
	back = (back + 1) % ring;
	while (heldAt[front] < n - delay)
		front = (front + 1) % ring;
	const double lowest = held[front];

	// The average of the last delay+1 lowest values. Each of them covers the sample now leaving the delay, so the
	// average never asks for more gain than that sample allows.
	const size_t window = averaged.size();
	const size_t slot = static_cast<size_t>(n % static_cast<long long>(window));
	sum += lowest - averaged[slot];
	averaged[slot] = lowest;
	if (slot == window - 1) { // once per window, add it up again so rounding cannot build up
		sum = 0;
		for (double v : averaged)
			sum += v;
	}
	const double average = std::min(1.0, sum / static_cast<double>(window));
	n++;

	// Down at once to what is needed, back up at 12 dB per release time.
	const double wanted = 20 * std::log10(std::max(average, 1e-12));
	gainDb = std::min(wanted, gainDb + releaseStepDb);
	return gainDb;
}

void Processor::process(float **channels, int count, size_t frames)
{
	count = std::min(count, maxChannels);
	for (size_t i = 0; i < frames; i++) {
		double need[maxChannels];
		double needAll = 1;
		for (int c = 0; c < count; c++) {
			const float x = static_cast<float>(channels[c][i] * boost_);
			line_[c][write_] = x;
			need[c] = std::min(1.0, ceiling_ / std::max(std::abs(static_cast<double>(x)), 1e-12));
			needAll = std::min(needAll, need[c]);
		}
		const double linked = s_.linkChannels ? path_[0].next(needAll, delay_, releaseStepDb_) : 0;
		const size_t read = (write_ + size_ - static_cast<size_t>(delay_)) % size_;
		for (int c = 0; c < count; c++) {
			const double gainDb = s_.linkChannels ? linked : path_[c].next(need[c], delay_, releaseStepDb_);
			const double y = line_[c][read] * std::pow(10, gainDb / 20);
			// The ceiling is a hard one: rounding can never take a sample past it.
			if (std::abs(y) > ceiling_ * (1 + 1e-9))
				clamped_++;
			channels[c][i] = static_cast<float>(std::clamp(y, -ceiling_, ceiling_));
		}
		write_ = (write_ + 1) % size_;
	}
}

} // namespace sarxina::limiter
