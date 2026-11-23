// A rough count of the words said into a mic, from its sound alone: no speech recognition, no model to download.
//
// Every syllable is a burst of loudness in the voice band (300-2500 Hz) on a voiced sound. So the sound is brought to
// 16 kHz and cut into 10 ms frames; a frame is a syllable when its loudness (smoothed over three frames) peaks at least
// 3 dB over the dip since the last syllable, sits within 25 dB of the loudest recent sound and 10 dB over the room,
// comes 100 ms or more after the last one, and is voiced (its sound repeats at a pitch of 80-400 Hz). Words are the
// syllables over 1.274, measured on Sarxina's stream of 2026-11-12 against a transcript of its 21,116 words (the
// count came within 1%, and within 3% on most 5-minute stretches).
//
// Plain arithmetic on samples, so the self-test and the wordcount tool run it without OBS.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sarxina::keeptalking {

class WordCounter {
public:
	static constexpr double kSyllablesPerWord = 1.274;

	explicit WordCounter(double sampleRate = 48000);

	// Mono samples at the rate given above, in pieces of any size.
	void add(const float *samples, size_t count);
	void reset();

	uint64_t syllables() const { return syllables_; }
	double words() const { return static_cast<double>(syllables_) / kSyllablesPerWord; }
	// Seconds of sound heard since the start or the last reset.
	double seconds() const { return static_cast<double>(heard16k_) / 16000.0; }

private:
	struct Biquad {
		double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		double z1 = 0, z2 = 0;
		double run(double x)
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	void add16k(float sample);
	void frameDone(double db);
	void judge(); // decides the frame two frames back, now that its neighbours are known
	double voicing() const; // of the frame being judged

	double rate_;
	// Bringing the sound to 16 kHz: a low-pass at 7 kHz, then a sample every rate/16000 input samples (interpolated).
	std::array<Biquad, 2> antiAlias_{};
	double step_ = 3, phase_ = 0;
	float previous_ = 0;
	bool resample_ = true;

	std::array<Biquad, 2> band_{};
	std::vector<float> history_; // the last samples at 16 kHz, for the voicing
	uint64_t heard16k_ = 0;
	double energy_ = 0;
	int inFrame_ = 0;

	// Loudness of the frames (dB), newest last: raw for the last three, smoothed for the last three.
	std::array<double, 3> raw_{};
	std::array<double, 3> smooth_{};
	uint64_t frames_ = 0;

	double floor_ = 0, loudest_ = -200, valley_ = 1e9;
	int64_t lastSyllable_ = -1000000;
	uint64_t syllables_ = 0;
};

} // namespace sarxina::keeptalking
