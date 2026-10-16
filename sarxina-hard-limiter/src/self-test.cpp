// Sarxina Hard Limiter's self-test (see shared/src/self-test.hpp). It needs no OBS and plays no sound; every check is
// a number worked out from the definitions in limiter.hpp.
//
//   1. The ceiling: bursts up to +20 dB over it, on two channels, with several look-aheads, linked and not: no sample
//      out is over the maximum amplitude, and the look-ahead alone keeps it there (the safety clamp never acts).
//   2. Below the ceiling nothing changes: the audio comes out exactly as it went in, late by the look-ahead.
//   3. The input boost: a quiet tone comes out louder by exactly the boost.
//   4. One peak in a steady signal: the gain is at what the peak needs when the peak comes out, starts going down one
//      look-ahead before, and comes back at 12 dB per release time.
//   5. Linked channels: a peak on one channel turns the other down by the same amount; unlinked, it does not.
//   6. The settings survive a trip through OBS's settings data.
#include "self-test.hpp"

#include "limiter-settings.hpp"
#include "limiter.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::limiter;

namespace {

constexpr double rate = 48000;

struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(bool ok, const std::string &what)
	{
		total++;
		if (ok)
			passed++;
		else if (failed.size() < 300)
			failed += (failed.empty() ? "; failed: " : ", ") + what;
	}
	void near(double got, double want, double within, const std::string &what)
	{
		char line[200];
		std::snprintf(line, sizeof line, "%s %.4f, expected %.4f", what.c_str(), got, want);
		expect(std::abs(got - want) <= within, line);
	}
	bool report(const char *what)
	{
		const bool ok = passed == total;
		test::report(ok, "%s: %d of %d as expected%s: %s", what, passed, total, failed.c_str(), ok ? "OK" : "FAILED");
		return ok;
	}
};

double db(double v)
{
	return 20 * std::log10(std::max(std::abs(v), 1e-12));
}

// Runs two channels through a processor in 480-sample blocks, as OBS hands audio over.
void run(Processor &p, std::vector<float> &left, std::vector<float> &right)
{
	for (size_t at = 0; at < left.size(); at += 480) {
		float *planes[2] = {left.data() + at, right.data() + at};
		p.process(planes, 2, std::min<size_t>(480, left.size() - at));
	}
}

bool ceiling()
{
	Tally t;
	std::mt19937 random(7);
	std::normal_distribution<double> noise(0, 1);
	for (double lookahead : {0.0, 1.0, 7.0, 20.0}) {
		for (bool link : {true, false}) {
			Settings s;
			s.lookaheadMs = lookahead;
			s.linkChannels = link;
			s.inputBoostDb = 6;
			Processor p(rate);
			p.setSettings(s);
			// Bursts of noise at random levels up to +20 dB over the ceiling, different on each channel.
			std::vector<float> left(static_cast<size_t>(rate * 3)), right(left.size());
			for (size_t i = 0; i < left.size(); i++) {
				const double burst = std::pow(10, (std::fmod(i / 4800.0, 1.0) * 30 - 10) / 20);
				left[i] = static_cast<float>(noise(random) * 0.3 * burst);
				right[i] = static_cast<float>(noise(random) * 0.2 * burst * (i % 9600 < 4800 ? 1 : 0.1));
			}
			run(p, left, right);
			double loudest = 0;
			for (size_t i = 0; i < left.size(); i++)
				loudest = std::max({loudest, std::abs(double(left[i])), std::abs(double(right[i]))});
			const std::string name = std::to_string(int(lookahead)) + " ms" + (link ? " linked" : " unlinked");
			t.expect(db(loudest) <= s.maximumAmplitudeDb + 1e-6, name + ": loudest " + std::to_string(db(loudest)) + " dB");
			if (lookahead > 0)
				t.expect(p.clamped() == 0, name + ": the safety clamp acted " + std::to_string(p.clamped()) + " times");
		}
	}
	return t.report("the ceiling, 3 s of bursts up to +20 dB over it");
}

bool belowAndBoost()
{
	Tally t;
	Settings s; // his: -1.5 dB, 7 ms, 100 ms, linked
	Processor p(rate);
	p.setSettings(s);
	const int delay = p.latencySamples();
	t.expect(delay == 336, "7 ms look-ahead is " + std::to_string(delay) + " samples, expected 336");
	std::vector<float> left(static_cast<size_t>(rate)), right(left.size());
	for (size_t i = 0; i < left.size(); i++)
		left[i] = right[i] = static_cast<float>(0.5 * std::sin(i * 0.0431)); // -6 dB, under the ceiling
	const std::vector<float> in = left;
	run(p, left, right);
	double worst = 0;
	for (size_t i = delay; i < left.size(); i++)
		worst = std::max(worst, std::abs(double(left[i]) - double(in[i - delay])));
	t.expect(worst == 0, "under the ceiling: largest change " + std::to_string(worst));

	s.inputBoostDb = 4.5;
	Processor boosted(rate);
	boosted.setSettings(s);
	std::vector<float> quiet(static_cast<size_t>(rate)), other(quiet.size());
	for (size_t i = 0; i < quiet.size(); i++)
		quiet[i] = other[i] = static_cast<float>(0.1 * std::sin(i * 0.0431)); // -20 dB
	run(boosted, quiet, other);
	double peak = 0;
	for (size_t i = delay; i < quiet.size(); i++)
		peak = std::max(peak, std::abs(double(quiet[i])));
	t.near(db(peak), -20 + 4.5, 0.01, "4.5 dB boost of a -20 dB tone");
	return t.report("below the ceiling, and the input boost");
}

bool onePeak()
{
	Tally t;
	Settings s; // his
	Processor p(rate);
	p.setSettings(s);
	const int delay = p.latencySamples();
	const double level = 0.25, spike = 2.0; // -12 dB steady, one sample at +6 dB
	const size_t at = 24000;
	std::vector<float> left(static_cast<size_t>(rate)), right(left.size());
	for (size_t i = 0; i < left.size(); i++)
		left[i] = right[i] = static_cast<float>(i == at ? spike : level);
	run(p, left, right);
	const size_t out = at + delay; // where the peak comes out
	const double needed = s.maximumAmplitudeDb - db(spike);
	auto gainAt = [&](size_t i) { return db(left[i] / (i == out ? spike : level)); };
	t.near(gainAt(out), needed, 0.001, "gain on the peak (dB)");
	t.near(gainAt(out - delay - 1), 0, 1e-6, "gain one look-ahead before the peak");
	t.expect(gainAt(out - delay) < 0, "the gain has started down one look-ahead before the peak");
	bool falling = true;
	for (size_t i = out - delay; i < out; i++)
		falling = falling && gainAt(i + 1) <= gainAt(i) + 1e-9;
	t.expect(falling, "the gain only goes down over the look-ahead");
	// Back up at 12 dB per 100 ms: 3 dB after 25 ms, all the way after needed/12 * 100 ms.
	t.near(gainAt(out + size_t(rate * 0.025)), needed + 3, 0.01, "gain 25 ms after the peak (dB)");
	const double back = -needed / 12 * 0.1;
	t.near(gainAt(out + size_t(rate * back) + 2), 0, 1e-6, "gain once 12 dB per release time has brought it back");
	t.expect(gainAt(out + size_t(rate * back) - 48) < -0.1, "not back before then");
	return t.report("one peak: look-ahead, gain on the peak, release");
}

bool linking()
{
	Tally t;
	for (bool link : {true, false}) {
		Settings s;
		s.linkChannels = link;
		Processor p(rate);
		p.setSettings(s);
		const int delay = p.latencySamples();
		std::vector<float> left(static_cast<size_t>(rate / 2), 0.25f), right(left.size(), 0.25f);
		left[12000] = 2.0f;
		run(p, left, right);
		const double other = db(right[12000 + delay] / 0.25);
		if (link)
			t.near(other, s.maximumAmplitudeDb - db(2.0), 0.001, "linked: the quiet channel's gain at the peak");
		else
			t.near(other, 0, 1e-6, "unlinked: the quiet channel's gain at the peak");
	}
	return t.report("linked and unlinked channels");
}

bool roundTrip()
{
	Settings s;
	s.maximumAmplitudeDb = -3.2;
	s.inputBoostDb = 2.5;
	s.lookaheadMs = 4.4;
	s.releaseMs = 250;
	s.linkChannels = false;
	obs_data_t *data = obs_data_create();
	setDefaults(data);
	write(data, s);
	const Settings back = read(data);
	obs_data_release(data);
	Tally t;
	t.expect(back.maximumAmplitudeDb == s.maximumAmplitudeDb && back.inputBoostDb == s.inputBoostDb &&
			 back.lookaheadMs == s.lookaheadMs && back.releaseMs == s.releaseMs &&
			 back.linkChannels == s.linkChannels,
		 "values");
	obs_data_t *empty = obs_data_create();
	setDefaults(empty);
	const Settings defaults = read(empty);
	obs_data_release(empty);
	t.expect(defaults.maximumAmplitudeDb == -1.5 && defaults.inputBoostDb == 0 && defaults.lookaheadMs == 7 &&
			 defaults.releaseMs == 100 && defaults.linkChannels,
		 "defaults are his rack's (-1.5 dB, 0 dB, 7 ms, 100 ms, linked)");
	return t.report("settings through OBS's settings data and back");
}

} // namespace

bool runSelfTest(const char *)
{
	const bool ok = ceiling() & belowAndBoost() & onePeak() & linking() & roundTrip();
	test::report(ok, ok ? "passed" : "FAILED");
	return ok;
}
