// The limiter's settings as OBS keeps them: maximum_amplitude_db, input_boost_db, lookahead_ms, release_ms and
// link_channels. The defaults are Alec's stream voice rack (Audition's Hard Limiter on SarxinaFinalRealStreamVoice).
#pragma once

#include "limiter.hpp"

#include <obs-data.h>

namespace sarxina::limiter {

void setDefaults(obs_data_t *data);
Settings read(obs_data_t *data);
void write(obs_data_t *data, const Settings &settings);

} // namespace sarxina::limiter
