#include "gate-settings.hpp"

#include <algorithm>

namespace sarxina::gate {

namespace {

struct Key {
	const char *name;
	double Settings::*field;
	double low, high;
};

const Key keys[] = {
	{"open_db", &Settings::openDb, -96, 0},       {"close_db", &Settings::closeDb, -96, 0},
	{"range_db", &Settings::rangeDb, -96, 0},     {"attack_ms", &Settings::attackMs, 0, 500},
	{"hold_ms", &Settings::holdMs, 0, 2000},      {"release_ms", &Settings::releaseMs, 0, 5000},
	{"lookahead_ms", &Settings::lookaheadMs, 0, maxLookaheadMs},
};

} // namespace

void setDefaults(obs_data_t *data)
{
	const Settings s;
	for (const Key &k : keys)
		obs_data_set_default_double(data, k.name, s.*k.field);
}

Settings read(obs_data_t *data)
{
	Settings s;
	for (const Key &k : keys)
		s.*k.field = std::clamp(obs_data_get_double(data, k.name), k.low, k.high);
	return s;
}

void write(obs_data_t *data, const Settings &s)
{
	for (const Key &k : keys)
		obs_data_set_double(data, k.name, s.*k.field);
}

} // namespace sarxina::gate
