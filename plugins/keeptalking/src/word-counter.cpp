#include "word-counter.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina::keeptalking {

namespace {

constexpr double kRate = 16000;
constexpr int kFrame = 160;   // 10 ms
constexpr int kWindow = 480;  // the voicing looks at 30 ms: the frame and one each side
constexpr int kLagShort = 40; // 400 Hz
constexpr int kLagLong = 200; // up to 80 Hz (lags 40-199)

constexpr double kRise = 3.0;       // dB over the dip since the last syllable
constexpr double kBelowLoudest = 25; // dB under the loudest recent sound, at most
constexpr double kOverRoom = 10;    // dB over the room, at least
constexpr double kRoomRise = 0.01;  // dB per frame the room's level creeps up (1 dB a second)
constexpr double kLoudestFall = 0.002; // dB per frame the loudest recent sound fades (0.2 dB a second)
constexpr double kVoiced = 0.35;    // how much the sound must repeat at a pitch
constexpr int kGapFrames = 10;      // 100 ms between syllables, at least

constexpr double kPi = 3.14159265358979323846;
} // namespace

WordCounter::WordCounter(double sampleRate) : rate_(sampleRate > 0 ? sampleRate : 48000)
{
	reset();
}

void WordCounter::reset()
{
	resample_ = std::abs(rate_ - kRate) > 0.5;
	step_ = rate_ / kRate;
	phase_ = 0;
	previous_ = 0;
	// A low-pass at 7 kHz, Butterworth of order 4 as two biquads (the audio EQ cookbook's, one Q per pair of poles).
	const double qs[2] = {0.54119610014619701, 1.3065629648763766};
	for (int k = 0; k < 2; k++) {
		const double w = 2 * kPi * 7000.0 / rate_;
		const double alpha = std::sin(w) / (2 * qs[k]);
		const double a0 = 1 + alpha;
		Biquad &f = antiAlias_[k];
		f = Biquad{};
		f.b0 = (1 - std::cos(w)) / 2 / a0;
		f.b1 = (1 - std::cos(w)) / a0;
		f.b2 = f.b0;
		f.a1 = -2 * std::cos(w) / a0;
		f.a2 = (1 - alpha) / a0;
	}
	// The voice band, 300-2500 Hz at 16 kHz: scipy's butter(2, [300, 2500], "bandpass", fs=16000, output="sos").
	band_[0] = Biquad{0.11398668088616984, 0.22797336177233968, 0.11398668088616984, -0.8313447891074905,
			  0.35168178501569014};
	band_[1] = Biquad{1, -2, 1, -1.8395022405143984, 0.8549029264403619};
	history_.assign(kWindow + kFrame, 0.f);
	heard16k_ = 0;
	energy_ = 0;
	inFrame_ = 0;
	raw_ = {};
	smooth_ = {};
	frames_ = 0;
	floor_ = 0;
	loudest_ = -200;
	valley_ = 1e9;
	lastSyllable_ = -1000000;
	syllables_ = 0;
}

void WordCounter::add(const float *samples, size_t count)
{
	if (!samples)
		return;
	for (size_t i = 0; i < count; i++) {
		const float in = std::isfinite(samples[i]) ? samples[i] : 0.f;
		if (!resample_) {
			add16k(in);
			continue;
		}
		const float now = static_cast<float>(antiAlias_[1].run(antiAlias_[0].run(in)));
		// Output samples fall at phase_ (in input samples, from the previous input sample).
		while (phase_ <= 1.0) {
			add16k(static_cast<float>(previous_ + (now - previous_) * phase_));
			phase_ += step_;
		}
		phase_ -= 1.0;
		previous_ = now;
	}
}

void WordCounter::add16k(float sample)
{
	// The last four frames, oldest first: the one being filled sits at kWindow .. kWindow + kFrame.
	history_[kWindow + inFrame_] = sample;
	heard16k_++;
	const double band = band_[1].run(band_[0].run(sample));
	energy_ += band * band;
	if (++inFrame_ < kFrame)
		return;
	const double db = 10 * std::log10(energy_ / kFrame + 1e-12);
	energy_ = 0;
	inFrame_ = 0;
	frameDone(db);
	std::copy(history_.begin() + kFrame, history_.end(), history_.begin());
}

// history_ holds frames f-3 .. f (f just finished) as 4 x 160 samples.
void WordCounter::frameDone(double db)
{
	raw_ = {raw_[1], raw_[2], db};
	frames_++;
	if (frames_ < 3)
		return;
	// The loudness of frame f-1, smoothed over f-2 .. f.
	smooth_ = {smooth_[1], smooth_[2], (raw_[0] + raw_[1] + raw_[2]) / 3};
	if (frames_ == 3)
		floor_ = smooth_[2];
	if (frames_ < 5)
		return;
	judge();
}

// Frame i = f-2: its neighbours' smoothed loudness are smooth_[0] (i-1) and smooth_[2] (i+1).
void WordCounter::judge()
{
	const int64_t i = static_cast<int64_t>(frames_) - 3;
	const double here = smooth_[1];
	floor_ = here < floor_ ? here : floor_ + kRoomRise;
	loudest_ = std::max(loudest_ - kLoudestFall, here);
	valley_ = std::min(valley_, here);
	if (!(here >= smooth_[0] && here > smooth_[2]))
		return;
	if (here - valley_ < kRise || here <= loudest_ - kBelowLoudest || here <= floor_ + kOverRoom ||
	    i - lastSyllable_ < kGapFrames)
		return;
	if (voicing() < kVoiced)
		return;
	syllables_++;
	lastSyllable_ = i;
	valley_ = here;
}

// How much the 30 ms around the frame repeat at a pitch of 80-400 Hz: the highest autocorrelation at those lags over
// the one at none. history_ holds frames i-1, i, i+1 in its first kWindow samples.
double WordCounter::voicing() const
{
	double mean = 0;
	for (int t = 0; t < kWindow; t++)
		mean += history_[t];
	mean /= kWindow;
	float f[kWindow];
	for (int t = 0; t < kWindow; t++)
		f[t] = static_cast<float>(history_[t] - mean);
	double zero = 0;
	for (int t = 0; t < kWindow; t++)
		zero += static_cast<double>(f[t]) * f[t];
	if (zero <= 1e-7)
		return 0;
	double best = -1e300;
	for (int lag = kLagShort; lag < kLagLong; lag++) {
		double sum = 0;
		for (int t = 0; t + lag < kWindow; t++)
			sum += static_cast<double>(f[t]) * f[t + lag];
		best = std::max(best, sum);
	}
	return best / zero;
}

} // namespace sarxina::keeptalking
