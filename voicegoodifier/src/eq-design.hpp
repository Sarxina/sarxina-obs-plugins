// The equalizer itself, with no OBS in it: Adobe Audition's Parametric Equalizer layout (a high-pass, a low shelf,
// five bell bands, a high shelf and a low-pass, then a master gain), turned into biquad sections. The filter, the
// curve editor and the self-test all use this one design, so the curve on screen is the filter that runs.
#pragma once

#include <array>
#include <complex>
#include <vector>

namespace sarxina::eq {

// The bands left to right, as Audition lays them out.
enum Band { HighPass, LowShelf, Bell1, Bell2, Bell3, Bell4, Bell5, HighShelf, LowPass, BandCount };

enum class Kind { HighPass, LowShelf, Bell, HighShelf, LowPass };
Kind kindOf(int band);

struct BandSettings {
	bool on = false;
	double hz = 1000;
	double db = 0;     // bells and shelves
	double q = 2;      // bells, in Constant Q
	int slope = 12;    // high- and low-pass, dB per octave: 6 to 48 in steps of 6
	bool steep = true; // shelves: second order (12 dB per octave) rather than first order (6)
	double width = 500; // bells, in Constant Width: Hz between the band's edges, kept when the frequency moves
};

struct Settings {
	std::array<BandSettings, BandCount> bands;
	double gainDb = 0; // master gain, after the bands
	bool constantWidth = false; // bells keep their width in Hz (Audition's "Constant: Width"), else their Q
	bool wideRange = false; // the editor shows +-48 dB instead of +-15 (Audition's 96 dB and 30 dB ranges)

	// The Q a bell has now, in either mode (a width w at f is a Q of f / w).
	double qOf(int band) const;

	// Audition's Parametric Equalizer as it opens: everything on except the two pass filters, all at 0 dB.
	static Settings defaults();

	// Alec's stream voice EQ (Audition session SarxinaFinalRealStreamVoice, slot 1): the voice chain's EQ as it opens.
	static Settings streamVoice();
};

constexpr double minHz = 20;
constexpr double maxHz = 22000;
constexpr double maxDb = 48; // bells, shelves and the master gain go from -maxDb to +maxDb
constexpr double minQ = 0.1;
constexpr double maxQ = 100;
constexpr double minWidth = 1;
constexpr double maxWidth = 20000;

// One second-order section, normalised so that a0 = 1: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct Biquad {
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	std::complex<double> at(double hz, double sampleRate) const;
};

// The sections of every band that is on, in order, and the master gain as a factor.
struct Design {
	std::vector<Biquad> sections;
	double gain = 1;

	static Design of(const Settings &settings, double sampleRate);
	// The design's response in dB at a frequency.
	double db(double hz, double sampleRate) const;
};

// One band's own sections (none when the band is off or does nothing). `q` is the bell's Q.
std::vector<Biquad> sectionsOf(int band, const BandSettings &settings, double q, double sampleRate);

// Runs a design over audio, one channel at a time, keeping each section's state between calls. A new design can be
// set at any time between blocks; sections that are still there keep their state, so changes do not click.
class Processor {
public:
	Processor(); // makes room for the most sections a design can have, so setDesign does not allocate
	void setDesign(const Design &design);
	void reset();
	void process(float *samples, size_t frames, int channel);

	static constexpr int maxChannels = 8;

private:
	struct State {
		double z1 = 0, z2 = 0;
	};
	Design design_;
	std::array<std::vector<State>, maxChannels> state_;
};

} // namespace sarxina::eq
