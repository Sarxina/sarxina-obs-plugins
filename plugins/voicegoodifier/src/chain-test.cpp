// The chain's part of VoiceGoodifier's self-test (see tests.hpp). It needs no OBS and plays no sound.
//
//   1. A chain of one stage is that stage: noise through the Gate, EQ and Limiter chains comes out sample for sample
//      as it does through the gate, EQ and limiter on their own.
//   2. The voice chain with its defaults: late by the gate's and the limiter's look-ahead together; the room (a -70 dB
//      tone) comes out the gate's range down plus what the EQ does at that frequency; and bursts up to +20 dB over
//      the ceiling never go over it, though the EQ comes before the limiter and boosts the highs.
//   3. Settings: the voice chain's stages keep their own prefixed keys and open on his stream voice rack, and a filter
//      of one stage reads the keys the plugins VoiceGoodifier replaced wrote.
#include "tests.hpp"

#include "eq-settings.hpp"
#include "gate-settings.hpp"
#include "limiter-settings.hpp"
#include "voice-chain.hpp"

#include <algorithm>
#include <random>
#include <vector>

using namespace sarxina;
using namespace sarxina::voice;
using sarxina::voice::test::Tally;

namespace {

constexpr double rate = 48000;
constexpr double pi = 3.14159265358979323846;

// Two channels of audio, as OBS hands them to a filter.
struct Stereo {
	std::vector<float> left, right;

	explicit Stereo(size_t frames) : left(frames), right(frames) {}
	size_t size() const { return left.size(); }
};

Stereo noise(size_t frames, double amplitude, unsigned seed)
{
	std::mt19937 random(seed);
	std::normal_distribution<double> gauss(0, amplitude);
	Stereo s(frames);
	for (size_t i = 0; i < frames; i++) {
		s.left[i] = static_cast<float>(gauss(random));
		s.right[i] = static_cast<float>(gauss(random));
	}
	return s;
}

// Runs audio through anything with OBS's planar process(), in 480-sample blocks.
template<typename Run> void inBlocks(Stereo &audio, Run run)
{
	for (size_t at = 0; at < audio.size(); at += 480) {
		float *planes[2] = {audio.left.data() + at, audio.right.data() + at};
		run(planes, std::min<size_t>(480, audio.size() - at));
	}
}

// The chain's settings with every default, the way a new filter of that layout gets them from OBS.
ChainSettings defaultsOf(const Layout &layout)
{
	obs_data_t *data = obs_data_create();
	setDefaults(data, layout);
	const ChainSettings settings = read(data, layout, rate);
	obs_data_release(data);
	return settings;
}

bool sameAsStageAlone()
{
	Tally t;
	const Stereo in = noise(size_t(rate), 0.3, 11);

	Stereo viaChain = in, alone = in;
	Chain gateChain(Layout{GateStage}, rate);
	gateChain.setSettings(defaultsOf(Layout{GateStage}));
	gate::Processor gate(rate);
	inBlocks(viaChain, [&](float **p, size_t n) { gateChain.process(p, 2, n); });
	inBlocks(alone, [&](float **p, size_t n) { gate.process(p, 2, n); });
	t.expect(viaChain.left == alone.left && viaChain.right == alone.right, "the Gate chain against the gate");

	viaChain = in, alone = in;
	ChainSettings eqSettings;
	eqSettings.eq = eq::Design::of(eq::Settings::streamVoice(), rate);
	Chain eqChain(Layout{EqStage}, rate);
	eqChain.setSettings(eqSettings);
	eq::Processor eq;
	eq.setDesign(eqSettings.eq);
	inBlocks(viaChain, [&](float **p, size_t n) { eqChain.process(p, 2, n); });
	inBlocks(alone, [&](float **p, size_t n) {
		eq.process(p[0], n, 0);
		eq.process(p[1], n, 1);
	});
	t.expect(viaChain.left == alone.left && viaChain.right == alone.right, "the EQ chain against the EQ");

	viaChain = in, alone = in;
	Chain limiterChain(Layout{LimiterStage}, rate);
	limiterChain.setSettings(defaultsOf(Layout{LimiterStage}));
	limiter::Processor limiter(rate);
	inBlocks(viaChain, [&](float **p, size_t n) { limiterChain.process(p, 2, n); });
	inBlocks(alone, [&](float **p, size_t n) { limiter.process(p, 2, n); });
	t.expect(viaChain.left == alone.left && viaChain.right == alone.right, "the Limiter chain against the limiter");
	return t.report("Chain: one stage is that stage, sample for sample");
}

bool voiceChain()
{
	Tally t;
	const Layout layout{AllStages};
	const ChainSettings settings = defaultsOf(layout);
	Chain chain(layout, rate);
	chain.setSettings(settings);
	t.expect(chain.latencySamples() == 240 + 336,
		 "late by " + std::to_string(chain.latencySamples()) + " samples, expected 576 (5 ms + 7 ms)");

	// The room: a -70 dB tone at 1 kHz for 1.5 s; over the last 0.5 s (500 whole periods) it comes out the range down
	// and through the EQ.
	Stereo room(size_t(rate * 1.5));
	const double amplitude = std::pow(10, -70 / 20.0);
	for (size_t i = 0; i < room.size(); i++)
		room.left[i] = room.right[i] = static_cast<float>(amplitude * std::sin(2 * pi * 1000 * i / rate));
	inBlocks(room, [&](float **p, size_t n) { chain.process(p, 2, n); });
	double power = 0;
	for (size_t i = size_t(rate); i < room.size(); i++)
		power += double(room.left[i]) * room.left[i];
	const double outDb = 10 * std::log10(power / (rate / 2) * 2 / (amplitude * amplitude));
	const double eqDb = settings.eq.db(1000, rate);
	t.near(outDb, gate::Settings().rangeDb + eqDb, 0.05, "the room's gain (dB)");

	// Bursts up to +20 dB over the ceiling, through the gate and the EQ first.
	Chain loud(layout, rate);
	loud.setSettings(settings);
	Stereo bursts = noise(size_t(rate * 3), 0.3, 7);
	for (size_t i = 0; i < bursts.size(); i++) {
		const double burst = std::pow(10, (std::fmod(i / 4800.0, 1.0) * 30 - 10) / 20);
		bursts.left[i] *= static_cast<float>(burst);
		bursts.right[i] *= static_cast<float>(burst);
	}
	inBlocks(bursts, [&](float **p, size_t n) { loud.process(p, 2, n); });
	double loudest = 0;
	for (size_t i = 0; i < bursts.size(); i++)
		loudest = std::max({loudest, std::abs(double(bursts.left[i])), std::abs(double(bursts.right[i]))});
	t.expect(20 * std::log10(loudest) <= limiter::Settings().maximumAmplitudeDb + 1e-6,
		 "loudest " + std::to_string(20 * std::log10(loudest)) + " dB");
	return t.report("Chain: the voice chain with its defaults");
}

// The voice chain's defaults are his rack, and its stages keep apart under their prefixes.
void chainSettings(Tally &t)
{
	obs_data_t *data = obs_data_create();
	setDefaults(data, Layout{AllStages});
	t.expect(eq::read(data, "eq_").bands[eq::HighShelf].db == eq::Settings::streamVoice().bands[eq::HighShelf].db,
		 "the chain's EQ opens on the stream voice curve");
	t.expect(gate::read(data, "gate_").openDb == gate::Settings().openDb, "the chain's gate opens on his gate");
	gate::Settings g;
	g.releaseMs = 321;
	limiter::Settings l;
	l.releaseMs = 77;
	gate::write(data, g, "gate_");
	limiter::write(data, l, "limiter_");
	t.expect(gate::read(data, "gate_").releaseMs == 321 && limiter::read(data, "limiter_").releaseMs == 77,
		 "the gate's and the limiter's release keep apart");
	obs_data_release(data);
}

// A filter of one stage reads the keys the replaced plugins wrote, and opens on their defaults.
void replacedKeys(Tally &t)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_double(data, "open_db", -44);
	obs_data_set_double(data, "b3_hz", 2500);
	obs_data_set_double(data, "maximum_amplitude_db", -3);
	const Layout gateOnly{GateStage}, eqOnly{EqStage}, limiterOnly{LimiterStage};
	setDefaults(data, gateOnly);
	setDefaults(data, eqOnly);
	setDefaults(data, limiterOnly);
	t.expect(gate::read(data, gateOnly.prefix(GateStage)).openDb == -44, "Sarxina Noise Gate's open_db");
	t.expect(eq::read(data, eqOnly.prefix(EqStage)).bands[eq::Bell3].hz == 2500, "Sarxina Parametric EQ's b3_hz");
	t.expect(limiter::read(data, limiterOnly.prefix(LimiterStage)).maximumAmplitudeDb == -3,
		 "Sarxina Hard Limiter's maximum_amplitude_db");
	t.expect(eq::read(data).bands[eq::HighShelf].db == 0, "the EQ on its own opens flat, as Audition's does");
	obs_data_release(data);
}

bool settingsKeys()
{
	Tally t;
	chainSettings(t);
	replacedKeys(t);
	return t.report("Chain: settings keys and defaults");
}

} // namespace

bool sarxina::voice::test::chainTest()
{
	return sameAsStageAlone() & voiceChain() & settingsKeys();
}
