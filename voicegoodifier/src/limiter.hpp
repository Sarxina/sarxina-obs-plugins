// The limiter itself, with no OBS in it, working the way Adobe Audition's Hard Limiter is described: the input is
// boosted, then no sample may go over the maximum amplitude. The audio is delayed by the look-ahead time, and the gain
// starts going down that long before a peak arrives, so the peak is turned down smoothly instead of clipped. After a
// peak the gain comes back at 12 dB per release time (Audition's definition of its release). With linked channels,
// every channel gets the gain the loudest one needs.
#pragma once

#include <array>
#include <vector>

namespace sarxina::limiter {

struct Settings {
	double maximumAmplitudeDb = -1.5;
	double inputBoostDb = 0;
	double lookaheadMs = 7;
	double releaseMs = 100;
	bool linkChannels = true;
};

constexpr double maxLookaheadMs = 50;
constexpr int maxChannels = 8;

class Processor {
public:
	explicit Processor(double sampleRate);
	void setSettings(const Settings &settings);
	// Planar audio, `count` arrays of `frames` samples, processed in place.
	void process(float **channels, int count, size_t frames);
	int latencySamples() const { return delay_; }
	// How many samples the final safety clamp had to change (more than rounding): 0 when the look-ahead did its job.
	size_t clamped() const { return clamped_; }

private:
	// The gain one stream of samples needs: the lowest over the look-ahead, averaged over the look-ahead (so the gain
	// ramps down ahead of a peak but is never above what the peak needs), then released slowly.
	struct GainPath {
		std::vector<double> held;      // the min-window's candidates (a monotonic queue in a ring)
		std::vector<long long> heldAt; // when each candidate came in
		size_t front = 0, back = 0;    // ring positions of the queue
		std::vector<double> averaged;  // the last delay+1 held values, for the running average
		double sum = 0;
		long long n = 0;
		double gainDb = 0;
		double next(double need, int delay, double releaseStepDb);
		void reset(size_t size, int delay);
	};

	double rate_;
	Settings s_;
	double ceiling_ = 1, boost_ = 1, releaseStepDb_ = 0;
	int delay_ = 0;
	size_t size_ = 0, write_ = 0;
	size_t clamped_ = 0;
	std::array<std::vector<float>, maxChannels> line_;
	std::array<GainPath, maxChannels> path_; // one per channel, or path_[0] for all when linked
};

} // namespace sarxina::limiter
