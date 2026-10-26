// What every VoiceGoodifier filter runs, with no OBS in it: a gate, then an EQ, then a limiter, each there or not.
// The EQ, Gate and Limiter filters are chains of one stage; the Voice Chain filter has all three, in the order a voice
// wants them: the room is gated out before the EQ brightens it, and the limiter has the last word on the peaks.
#pragma once

#include "eq-design.hpp"
#include "gate.hpp"
#include "limiter.hpp"

#include <obs-data.h>
#include <obs-properties.h>

#include <memory>

namespace sarxina::voice {

// The stages a chain has, as bits.
enum Stage { GateStage = 1, EqStage = 2, LimiterStage = 4, AllStages = 7 };

// One kind of filter: which stages it has. A filter of several stages is the voice chain, whose settings keys carry
// each stage's prefix ("gate_", "eq_", "limiter_"), since its stages would otherwise share keys; a filter of one
// stage keeps the keys of the plugin it came from.
struct Layout {
	int stages = AllStages;

	bool has(Stage stage) const { return (stages & stage) != 0; }
	bool chain() const { return (stages & (stages - 1)) != 0; }
	const char *prefix(Stage stage) const;
};

// Everything a chain needs to run: each stage's settings, with the EQ already turned into biquads.
struct ChainSettings {
	gate::Settings gate;
	eq::Design eq;
	limiter::Settings limiter;
};

// The settings of a chain as OBS keeps them. The voice chain's EQ opens on Alec's stream voice curve, the EQ on its
// own on Audition's flat one; the gate and limiter open on his stream voice rack either way.
void setDefaults(obs_data_t *data, const Layout &layout);
ChainSettings read(obs_data_t *data, const Layout &layout, double sampleRate);

// Adds the gate's and the limiter's settings to a filter's properties: in a group each when the chain has several
// stages. The EQ's button, which needs the filter, is added by the filter.
void addStageProperties(obs_properties_t *props, const Layout &layout);

class Chain {
public:
	Chain(const Layout &layout, double sampleRate);
	void setSettings(const ChainSettings &settings);
	// Planar audio, `count` arrays of `frames` samples, processed in place.
	void process(float **channels, int count, size_t frames);
	// How late the sound comes out: the gate's look-ahead and the limiter's.
	int latencySamples() const;

private:
	std::unique_ptr<gate::Processor> gate_;
	std::unique_ptr<eq::Processor> eq_;
	std::unique_ptr<limiter::Processor> limiter_;
};

} // namespace sarxina::voice
