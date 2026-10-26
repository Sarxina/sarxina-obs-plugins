#include "eq-design.hpp"

#include <algorithm>
#include <cmath>

namespace sarxina::eq {

namespace {

constexpr double pi = 3.14159265358979323846;

// Keeps a corner frequency below the Nyquist frequency, where the bilinear transform still has room.
double corner(double hz, double sampleRate)
{
	return std::clamp(hz, 1.0, 0.49 * sampleRate);
}

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2)
{
	return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// Butterworth high- or low-pass of 1 to 8 poles (6 to 48 dB per octave): a first-order section for an odd order,
// then one biquad per pair of poles, each with its pair's Q.
void butterworth(std::vector<Biquad> &out, bool high, int order, double hz, double sampleRate)
{
	const double k = std::tan(pi * corner(hz, sampleRate) / sampleRate);
	if (order % 2 == 1) {
		const double a1 = (k - 1) / (k + 1);
		out.push_back(high ? Biquad{1 / (1 + k), -1 / (1 + k), 0, a1, 0} : Biquad{k / (1 + k), k / (1 + k), 0, a1, 0});
	}
	for (int pair = 1; pair <= order / 2; pair++) {
		const double angle = order % 2 == 1 ? pi * pair / order : pi * (2 * pair - 1) / (2.0 * order);
		const double q = 1 / (2 * std::cos(angle));
		const double norm = 1 / (1 + k / q + k * k);
		const double a1 = 2 * (k * k - 1) * norm, a2 = (1 - k / q + k * k) * norm;
		if (high)
			out.push_back({norm, -2 * norm, norm, a1, a2});
		else
			out.push_back({k * k * norm, 2 * k * k * norm, k * k * norm, a1, a2});
	}
}

// A bell (peaking) band: the gain at the centre, half of it (in dB) at the edges that Q sets.
Biquad bell(double hz, double db, double q, double sampleRate)
{
	const double a = std::pow(10, db / 40), w = 2 * pi * corner(hz, sampleRate) / sampleRate;
	const double alpha = std::sin(w) / (2 * q), c = std::cos(w);
	return normalised(1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
}

// A shelf: the full gain beyond the corner, half of it (in dB) at the corner. Second order rises at up to 12 dB per
// octave with no bump (shelf slope 1), first order at up to 6.
Biquad shelf(bool high, bool steep, double hz, double db, double sampleRate)
{
	const double a = std::pow(10, db / 40);
	if (!steep) {
		const double k = std::tan(pi * corner(hz, sampleRate) / sampleRate);
		if (high)
			return normalised(a * (a + k), a * (k - a), 0, 1 + k * a, k * a - 1, 0);
		return normalised(a * (1 + k * a), a * (k * a - 1), 0, a + k, k - a, 0);
	}
	const double w = 2 * pi * corner(hz, sampleRate) / sampleRate, c = std::cos(w);
	const double root = 2 * std::sqrt(a) * std::sin(w) / std::sqrt(2.0);
	if (high)
		return normalised(a * ((a + 1) + (a - 1) * c + root), -2 * a * ((a - 1) + (a + 1) * c),
				  a * ((a + 1) + (a - 1) * c - root), (a + 1) - (a - 1) * c + root,
				  2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - root);
	return normalised(a * ((a + 1) - (a - 1) * c + root), 2 * a * ((a - 1) - (a + 1) * c),
			  a * ((a + 1) - (a - 1) * c - root), (a + 1) + (a - 1) * c + root, -2 * ((a - 1) + (a + 1) * c),
			  (a + 1) + (a - 1) * c - root);
}

} // namespace

Kind kindOf(int band)
{
	switch (band) {
	case HighPass:
		return Kind::HighPass;
	case LowShelf:
		return Kind::LowShelf;
	case HighShelf:
		return Kind::HighShelf;
	case LowPass:
		return Kind::LowPass;
	default:
		return Kind::Bell;
	}
}

Settings Settings::defaults()
{
	Settings s;
	const double hz[BandCount] = {20, 40, 50, 200, 800, 3200, 12800, 18000, 20000};
	for (int band = 0; band < BandCount; band++) {
		BandSettings &b = s.bands[band];
		b.hz = hz[band];
		b.on = band != HighPass && band != LowPass;
		b.q = 2;
		b.width = hz[band] / 2;
		b.slope = 24;
		b.steep = false;
	}
	return s;
}

Settings Settings::streamVoice()
{
	// As parse_rack.py reads it and as Audition's own window showed it on 2026-10-16: Constant Width, gains +2.1,
	// -2.7, -2.8, -2.7 and +10.6 dB.
	Settings s = defaults();
	s.constantWidth = true;
	auto set = [&s](int band, bool on, double hz, double db, double q, double width) {
		s.bands[band].on = on;
		s.bands[band].hz = hz;
		s.bands[band].db = db;
		s.bands[band].q = q;
		s.bands[band].width = width;
	};
	set(HighPass, true, 79.55, 0, 2, 40);
	s.bands[HighPass].slope = 48;
	set(LowShelf, true, 40, 0, 2, 20);
	set(Bell1, true, 130.84, 2.053, 2.0, 65.42);
	set(Bell2, true, 246.75, -2.684, 1.5, 164.5);
	set(Bell3, true, 2230.57, -2.842, 10.8, 206.53);
	set(Bell4, true, 4312.46, -2.684, 9.3, 463.71);
	set(Bell5, true, 12800, 0, 2.0, 6400);
	set(HighShelf, true, 13884.18, 10.579, 2, 6942);
	s.bands[HighShelf].steep = true;
	set(LowPass, false, 18000.01, 0, 2, 9000);
	return s;
}

double Settings::qOf(int band) const
{
	const BandSettings &b = bands[band];
	return std::clamp(constantWidth ? b.hz / std::max(b.width, minWidth) : b.q, minQ, maxQ);
}

std::complex<double> Biquad::at(double hz, double sampleRate) const
{
	const std::complex<double> z1 = std::polar(1.0, -2 * pi * hz / sampleRate), z2 = z1 * z1;
	return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

std::vector<Biquad> sectionsOf(int band, const BandSettings &b, double q, double sampleRate)
{
	std::vector<Biquad> out;
	if (!b.on)
		return out;
	switch (kindOf(band)) {
	case Kind::HighPass:
	case Kind::LowPass:
		butterworth(out, kindOf(band) == Kind::HighPass, std::clamp(b.slope / 6, 1, 8), b.hz, sampleRate);
		break;
	case Kind::LowShelf:
	case Kind::HighShelf:
		if (b.db != 0)
			out.push_back(shelf(kindOf(band) == Kind::HighShelf, b.steep, b.hz, b.db, sampleRate));
		break;
	case Kind::Bell:
		if (b.db != 0)
			out.push_back(bell(b.hz, b.db, q, sampleRate));
		break;
	}
	return out;
}

Design Design::of(const Settings &settings, double sampleRate)
{
	Design d;
	for (int band = 0; band < BandCount; band++) {
		const std::vector<Biquad> sections = sectionsOf(band, settings.bands[band], settings.qOf(band), sampleRate);
		d.sections.insert(d.sections.end(), sections.begin(), sections.end());
	}
	d.gain = std::pow(10, settings.gainDb / 20);
	return d;
}

double Design::db(double hz, double sampleRate) const
{
	std::complex<double> h = gain;
	for (const Biquad &s : sections)
		h *= s.at(hz, sampleRate);
	return 20 * std::log10(std::max(std::abs(h), 1e-12));
}

Processor::Processor()
{
	const size_t most = 2 * 4 + 7; // two 8-pole pass filters and seven bands
	design_.sections.reserve(most);
	for (auto &states : state_)
		states.reserve(most);
}

void Processor::setDesign(const Design &design)
{
	design_ = design;
	for (auto &states : state_)
		states.resize(design_.sections.size());
}

void Processor::reset()
{
	for (auto &states : state_)
		std::fill(states.begin(), states.end(), State{});
}

void Processor::process(float *samples, size_t frames, int channel)
{
	if (channel < 0 || channel >= maxChannels)
		return;
	std::vector<State> &states = state_[channel];
	const size_t count = design_.sections.size();
	for (size_t i = 0; i < frames; i++) {
		double x = samples[i];
		for (size_t n = 0; n < count; n++) {
			const Biquad &s = design_.sections[n];
			State &z = states[n];
			const double y = s.b0 * x + z.z1;
			z.z1 = s.b1 * x - s.a1 * y + z.z2;
			z.z2 = s.b2 * x - s.a2 * y;
			x = y;
		}
		samples[i] = static_cast<float>(x * design_.gain);
	}
}

} // namespace sarxina::eq
