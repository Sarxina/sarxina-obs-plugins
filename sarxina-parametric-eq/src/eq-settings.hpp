// The equalizer's settings as OBS keeps them: one key per number, named after the band ("b3_hz", "hs_db",
// "hp_slope"...), so a scene collection or a websocket request can set any of them.
#pragma once

#include "eq-design.hpp"

#include <obs-data.h>

namespace sarxina::eq {

// The key's prefix for each band: hp, ls, b1 to b5, hs, lp.
const char *bandKey(int band);

void setDefaults(obs_data_t *data);
Settings read(obs_data_t *data);
void write(obs_data_t *data, const Settings &settings);

} // namespace sarxina::eq
