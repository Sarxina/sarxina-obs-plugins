// The gate's settings as OBS keeps them: open_db, close_db, range_db, attack_ms, hold_ms, release_ms, lookahead_ms.
#pragma once

#include "gate.hpp"

#include <obs-data.h>

namespace sarxina::gate {

void setDefaults(obs_data_t *data);
Settings read(obs_data_t *data);
void write(obs_data_t *data, const Settings &settings);

} // namespace sarxina::gate
