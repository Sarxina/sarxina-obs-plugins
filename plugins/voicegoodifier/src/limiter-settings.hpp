// The limiter's settings as OBS keeps them: maximum_amplitude_db, input_boost_db, lookahead_ms, release_ms and
// link_channels, each after a prefix ("" for the limiter on its own, "limiter_" in the voice chain). The defaults are
// Alec's stream voice rack (Audition's Hard Limiter on SarxinaFinalRealStreamVoice).
#pragma once

#include "limiter.hpp"

#include <obs-data.h>
#include <obs-properties.h>

namespace sarxina::limiter {

void setDefaults(obs_data_t *data, const char *prefix = "");
Settings read(obs_data_t *data, const char *prefix = "");
void write(obs_data_t *data, const Settings &settings, const char *prefix = "");

// Adds the limiter's sliders and its link switch to a filter's properties.
void addProperties(obs_properties_t *props, const char *prefix = "");

} // namespace sarxina::limiter
