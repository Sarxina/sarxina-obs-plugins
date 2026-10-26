// The noise gate itself, with no OBS in it. It does the job of Alec's Audition "Noise Gate @ 20dB" curve (voice above
// about -50 dB untouched, the room under it pushed far down) as a gate built for a voice:
//
//   level       RMS over the last 10 ms, of the loudest channel (one gain for all channels)
//   open        the level at or over the open threshold opens the gate
//   close       it starts to close only once the level is under the close threshold, which sits lower (hysteresis),
//               so a word that dips a little does not flutter the gate
//   hold        after the level drops under the close threshold the gate stays open this long: word tails and the
//               gaps between words stay whole
//   attack      the time to go from closed to fully open
//   release     the time to go from fully open to closed: tails and breaths fade out instead of being cut off
//   range       how far down the closed gate turns the sound
//   look-ahead  the sound is delayed this long, so the gate is already open when a word starts
//
// The gain moves in straight lines in dB: attack and release are the whole trip between open and range.
#pragma once

#include <array>
#include <vector>

namespace sarxina::gate {

struct Settings {
	double openDb = -50;     // his curve leaves the voice alone from -50 dB up
	double closeDb = -56;    // 6 dB of hysteresis
	double rangeDb = -30;    // his curve takes the noise floor 18 to 30 dB down (-60 in to -78, -67 in to -97)
	double attackMs = 2;
	double holdMs = 150;
	double releaseMs = 250;  // his Audition gate's gain release
	double lookaheadMs = 5;
};

constexpr double levelWindowMs = 10;
constexpr double maxLookaheadMs = 50;
constexpr int maxChannels = 8;

class Processor {
public:
	explicit Processor(double sampleRate);
	void setSettings(const Settings &settings);
	// Planar audio, `count` arrays of `frames` samples, processed in place.
	void process(float **channels, int count, size_t frames);
	int latencySamples() const { return delay_; }
	bool open() const { return open_; }
	double gainDb() const { return gainDb_; }

private:
	double rate_;
	Settings s_;
	int delay_ = 0;
	long long holdSamples_ = 0;
	double attackStep_ = 0, releaseStep_ = 0; // dB per sample

	// The level: a running sum of the loudest channel's power over the window.
	std::vector<double> power_;
	double powerSum_ = 0;
	size_t powerAt_ = 0;

	bool open_ = false;
	long long holdLeft_ = 0;
	double gainDb_;

	size_t size_ = 0, write_ = 0;
	std::array<std::vector<float>, maxChannels> line_;
};

} // namespace sarxina::gate
