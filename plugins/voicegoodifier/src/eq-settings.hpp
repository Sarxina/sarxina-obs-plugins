// The equalizer's settings as OBS keeps them: one key per number, named after the band ("b3_hz", "hs_db",
// "hp_slope"...), so a scene collection or a websocket request can set any of them. Each key comes after a prefix:
// "" for the EQ on its own, "eq_" in the voice chain.
#pragma once

#include "eq-design.hpp"

#include <obs-data.h>

namespace sarxina::eq {

// The key's prefix for each band: hp, ls, b1 to b5, hs, lp.
const char *bandKey(int band);

// `defaults` is what a new filter opens with: Audition's Parametric Equalizer as it opens, unless told otherwise.
void setDefaults(obs_data_t *data, const char *prefix = "", const Settings &defaults = Settings::defaults());
Settings read(obs_data_t *data, const char *prefix = "");
void write(obs_data_t *data, const Settings &settings, const char *prefix = "");

} // namespace sarxina::eq
