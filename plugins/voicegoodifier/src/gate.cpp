#include "gate.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina::gate {

Processor::Processor(double sampleRate) : rate_(sampleRate), gainDb_(Settings().rangeDb)
{
	size_ = static_cast<size_t>(std::ceil(maxLookaheadMs * sampleRate / 1000)) + 1;
	for (auto &line : line_)
		line.assign(size_, 0.0f);
	power_.assign(static_cast<size_t>(std::lround(levelWindowMs * sampleRate / 1000)), 0.0);
	setSettings(Settings());
}

void Processor::setSettings(const Settings &settings)
{
	s_ = settings;
	s_.closeDb = std::min(s_.closeDb, s_.openDb);
	s_.rangeDb = std::min(s_.rangeDb, 0.0);
	delay_ = static_cast<int>(std::lround(std::clamp(s_.lookaheadMs, 0.0, maxLookaheadMs) * rate_ / 1000));
	holdSamples_ = std::llround(std::max(s_.holdMs, 0.0) * rate_ / 1000);
	const double span = -s_.rangeDb;
	attackStep_ = s_.attackMs > 0 ? span / (s_.attackMs * rate_ / 1000) : span;
	releaseStep_ = s_.releaseMs > 0 ? span / (s_.releaseMs * rate_ / 1000) : span;
	gainDb_ = std::clamp(gainDb_, s_.rangeDb, 0.0);
}

void Processor::process(float **channels, int count, size_t frames)
{
	count = std::min(count, maxChannels);
	const size_t window = power_.size();
	for (size_t i = 0; i < frames; i++) {
		// The loudest channel's power, into the window's running sum.
		double power = 0;
		for (int c = 0; c < count; c++) {
			const double x = channels[c][i];
			power = std::max(power, x * x);
			line_[c][write_] = channels[c][i];
		}
		powerSum_ += power - power_[powerAt_];
		power_[powerAt_] = power;
		powerAt_ = (powerAt_ + 1) % window;
		if (powerAt_ == 0) { // once per window, add it up again so rounding cannot build up
			powerSum_ = 0;
			for (double v : power_)
				powerSum_ += v;
		}
		const double level = 10 * std::log10(std::max(powerSum_ / window, 1e-20));

		// Open at the open threshold; under the close threshold, close once the hold has run out.
		if (level >= s_.openDb) {
			open_ = true;
			holdLeft_ = holdSamples_;
		} else if (open_) {
			if (level >= s_.closeDb)
				holdLeft_ = holdSamples_;
			else if (holdLeft_ > 0)
				holdLeft_--;
			else
				open_ = false;
		}
		gainDb_ = open_ ? std::min(0.0, gainDb_ + attackStep_) : std::max(s_.rangeDb, gainDb_ - releaseStep_);

		// The sound comes out `delay_` samples late, so the gain reaches a word before the word does.
		const size_t read = (write_ + size_ - static_cast<size_t>(delay_)) % size_;
		const double gain = std::pow(10, gainDb_ / 20);
		for (int c = 0; c < count; c++)
			channels[c][i] = static_cast<float>(line_[c][read] * gain);
		write_ = (write_ + 1) % size_;
	}
}

} // namespace sarxina::gate
