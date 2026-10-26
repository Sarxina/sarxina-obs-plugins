// The gate's settings as OBS keeps them: open_db, close_db, range_db, attack_ms, hold_ms, release_ms, lookahead_ms,
// each after a prefix ("" for the gate on its own, "gate_" in the voice chain).
#pragma once

#include "gate.hpp"

#include <obs-data.h>
#include <obs-properties.h>

namespace sarxina::gate {

void setDefaults(obs_data_t *data, const char *prefix = "");
Settings read(obs_data_t *data, const char *prefix = "");
void write(obs_data_t *data, const Settings &settings, const char *prefix = "");

// Adds the gate's sliders to a filter's properties.
void addProperties(obs_properties_t *props, const char *prefix = "");

} // namespace sarxina::gate
