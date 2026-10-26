// The gate's part of VoiceGoodifier's self-test (see tests.hpp). It needs no OBS and plays no sound; every check is
// a number worked out from the definitions in gate.hpp, with the default settings (his voice gate).
//
//   1. A word starting out of silence comes out whole: from its first sample it leaves at exactly the level it came
//      in, late by the look-ahead (the gate opened before it arrived).
//   2. The noise floor (-70 dB) comes out exactly the range (30 dB) down.
//   3. Hysteresis: a level between the close and open thresholds leaves an open gate open and a closed gate closed.
//   4. After the voice stops: the gate stays fully open for the hold time after the level falls under the close
//      threshold, then goes down in a straight line in dB over the release time, to the range.
//   5. Opening: from closed to fully open in the attack time.
//   6. The settings survive a trip through OBS's settings data, and the defaults are the ones in gate.hpp.
#include "tests.hpp"

#include "gate-settings.hpp"
#include "gate.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::gate;

using sarxina::voice::test::Tally;

namespace {

constexpr double rate = 48000;

double fromDb(double db)
{
	return std::pow(10, db / 20);
}

// Runs a signal (the same on two channels) through a processor in 480-sample blocks, as OBS hands audio over, and
// gives back the left channel. `gains` gets the gate's gain after each sample.
std::vector<float> run(Processor &p, const std::vector<float> &in, std::vector<double> *gains = nullptr)
{
	std::vector<float> left = in, right = in;
	for (size_t at = 0; at < in.size(); at += 480) {
		const size_t frames = std::min<size_t>(480, in.size() - at);
		if (!gains) {
			float *planes[2] = {left.data() + at, right.data() + at};
			p.process(planes, 2, frames);
			continue;
		}
		for (size_t i = 0; i < frames; i++) {
			float *planes[2] = {left.data() + at + i, right.data() + at + i};
			p.process(planes, 2, 1);
			gains->push_back(p.gainDb());
		}
	}
	return left;
}

bool wordsAndFloor()
{
	Tally t;
	Processor p(rate);
	const int delay = p.latencySamples();
	t.expect(delay == 240, "5 ms look-ahead is " + std::to_string(delay) + " samples, expected 240");

	// 1 s of silence, then a word: a -20 dB tone for 0.5 s.
	std::vector<float> in(int(rate * 1.5), 0.0f);
	for (size_t i = size_t(rate); i < in.size(); i++)
		in[i] = static_cast<float>(fromDb(-20) * std::sin((i - rate) * 0.0731));
	const std::vector<float> out = run(p, in);
	double worst = 0;
	for (size_t i = size_t(rate) + delay; i < out.size(); i++)
		worst = std::max(worst, std::abs(double(out[i]) - double(in[i - delay])));
	t.expect(worst == 0, "the word from its first sample: largest change " + std::to_string(worst));

	// The noise floor: a steady -70 dB.
	Processor floor(rate);
	std::vector<float> quiet(int(rate), static_cast<float>(fromDb(-70)));
	const std::vector<float> down = run(floor, quiet);
	t.near(20 * std::log10(down.back() / quiet.back()), Settings().rangeDb, 1e-6, "the -70 dB floor, gain (dB)");
	return t.report("Gate: words and the noise floor");
}

bool hysteresis()
{
	Tally t;
	const float between = static_cast<float>(fromDb(-53)); // between close (-56) and open (-50)
	// Open first (a -20 dB second), then 2 s between the thresholds: stays open.
	Processor a(rate);
	std::vector<float> in(int(rate * 3), between);
	for (size_t i = 0; i < size_t(rate); i++)
		in[i] = static_cast<float>(fromDb(-20));
	std::vector<double> gains;
	run(a, in, &gains);
	t.near(gains.back(), 0, 0, "between the thresholds after being open (dB)");
	// Closed first (silence), then the same level: stays closed.
	Processor b(rate);
	std::vector<float> fromSilence(int(rate * 3), between);
	for (size_t i = 0; i < size_t(rate); i++)
		fromSilence[i] = 0;
	gains.clear();
	run(b, fromSilence, &gains);
	t.near(gains.back(), Settings().rangeDb, 0, "between the thresholds after being closed (dB)");
	return t.report("Gate: hysteresis");
}

bool holdReleaseAttack()
{
	Tally t;
	const Settings s;
	const double perMs = rate / 1000;
	// 1 s at -20 dB, then a steady -70 dB. The 10 ms level window has emptied of the loud part at sample T + W - 1;
	// from there the gate holds for the hold time, then releases over the release time.
	Processor p(rate);
	const size_t T = size_t(rate), window = size_t(levelWindowMs * perMs);
	std::vector<float> in(int(rate * 2), static_cast<float>(fromDb(-70)));
	for (size_t i = 0; i < T; i++)
		in[i] = static_cast<float>(fromDb(-20));
	std::vector<double> gains;
	run(p, in, &gains);
	const size_t under = T + window - 1; // the first sample whose window holds only the quiet part
	const size_t hold = size_t(s.holdMs * perMs), release = size_t(s.releaseMs * perMs);
	t.near(gains[under + hold - 1], 0, 0, "fully open to the end of the hold");
	t.expect(gains[under + hold + 1] < 0, "going down just after the hold");
	t.near(gains[under + hold + release / 2], s.rangeDb / 2, 0.15, "halfway through the release (dB)");
	t.near(gains[under + hold + release + 2], s.rangeDb, 0, "closed at the end of the release (dB)");
	// The release is a straight line in dB.
	const double slope = (gains[under + hold + 3 * release / 4] - gains[under + hold + release / 4]) / (release / 2.0);
	t.near(slope * release, s.rangeDb, 0.05, "release slope over the whole release (dB)");

	// Opening: from closed, a -20 dB tone; fully open after the attack time.
	Processor q(rate);
	std::vector<float> word(int(rate), static_cast<float>(fromDb(-20)));
	for (size_t i = 0; i < size_t(rate / 2); i++)
		word[i] = 0;
	gains.clear();
	run(q, word, &gains);
	const size_t start = size_t(rate / 2), attack = size_t(s.attackMs * perMs);
	t.expect(gains[start - 1] == s.rangeDb, "closed before the word");
	t.near(gains[start + attack / 2 - 1], s.rangeDb / 2, 0.1, "halfway through the attack (dB)");
	t.near(gains[start + attack], 0, 0, "fully open after the attack");
	return t.report("Gate: hold, release and attack");
}

bool roundTrip()
{
	Settings s;
	s.openDb = -44.5;
	s.closeDb = -51;
	s.rangeDb = -40;
	s.attackMs = 1.5;
	s.holdMs = 200;
	s.releaseMs = 300;
	s.lookaheadMs = 3;
	obs_data_t *data = obs_data_create();
	setDefaults(data);
	write(data, s);
	const Settings back = read(data);
	obs_data_release(data);
	Tally t;
	t.expect(back.openDb == s.openDb && back.closeDb == s.closeDb && back.rangeDb == s.rangeDb &&
			 back.attackMs == s.attackMs && back.holdMs == s.holdMs && back.releaseMs == s.releaseMs &&
			 back.lookaheadMs == s.lookaheadMs,
		 "values");
	obs_data_t *empty = obs_data_create();
	setDefaults(empty);
	const Settings d = read(empty), want;
	obs_data_release(empty);
	t.expect(d.openDb == want.openDb && d.closeDb == want.closeDb && d.rangeDb == want.rangeDb &&
			 d.attackMs == want.attackMs && d.holdMs == want.holdMs && d.releaseMs == want.releaseMs &&
			 d.lookaheadMs == want.lookaheadMs,
		 "defaults");
	return t.report("Gate: settings through OBS's settings data and back");
}

} // namespace

bool sarxina::voice::test::gateTest()
{
	return wordsAndFloor() & hysteresis() & holdReleaseAttack() & roundTrip();
}
