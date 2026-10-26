// VoiceGoodifier's filters in OBS (Filters > Audio Filters > + on any audio source): VoiceGoodifier Gate, EQ and
// Limiter, and the Voice Chain with all three. The ids of the three plugins VoiceGoodifier replaced (Sarxina Noise
// Gate, Parametric EQ and Hard Limiter) are kept as the same filters, hidden from the + menu, so filters already on a
// source load with their settings as they were.
#pragma once

#include "voice-chain.hpp"

#include <obs-module.h>

namespace sarxina::voice {

struct Kind {
	const char *id;
	const char *nameKey; // its name in the + menu, from the strings
	Layout layout;
	bool replaced; // the id of a plugin VoiceGoodifier replaced
};

// Every kind of filter VoiceGoodifier registers, the replaced ids last.
const Kind *kinds(size_t &count);

void registerFilters();

// Runs a block of audio through a filter OBS made from one of the kinds, the way OBS does.
void runFilter(obs_source_t *filter, obs_audio_data *audio);

} // namespace sarxina::voice
