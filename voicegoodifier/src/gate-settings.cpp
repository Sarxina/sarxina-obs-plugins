#include "gate-settings.hpp"

#include <obs-module.h>

#include <algorithm>
#include <string>

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

std::string key(const char *prefix, const char *name)
{
	return std::string(prefix) + name;
}

void addSlider(obs_properties_t *props, const std::string &key, const char *text, double low, double high,
	       double step, const char *suffix)
{
	obs_property_t *p = obs_properties_add_float_slider(props, key.c_str(), obs_module_text(text), low, high, step);
	obs_property_float_set_suffix(p, suffix);
	obs_property_set_long_description(p, obs_module_text((std::string(text) + ".Help").c_str()));
}

} // namespace

void setDefaults(obs_data_t *data, const char *prefix)
{
	const Settings s;
	for (const Key &k : keys)
		obs_data_set_default_double(data, key(prefix, k.name).c_str(), s.*k.field);
}

Settings read(obs_data_t *data, const char *prefix)
{
	Settings s;
	for (const Key &k : keys)
		s.*k.field = std::clamp(obs_data_get_double(data, key(prefix, k.name).c_str()), k.low, k.high);
	return s;
}

void write(obs_data_t *data, const Settings &s, const char *prefix)
{
	for (const Key &k : keys)
		obs_data_set_double(data, key(prefix, k.name).c_str(), s.*k.field);
}

void addProperties(obs_properties_t *props, const char *prefix)
{
	addSlider(props, key(prefix, "open_db"), "Gate.Open", -96, 0, 0.5, " dB");
	addSlider(props, key(prefix, "close_db"), "Gate.Close", -96, 0, 0.5, " dB");
	addSlider(props, key(prefix, "range_db"), "Gate.Range", -96, 0, 0.5, " dB");
	addSlider(props, key(prefix, "attack_ms"), "Gate.Attack", 0, 100, 0.5, " ms");
	addSlider(props, key(prefix, "hold_ms"), "Gate.Hold", 0, 1000, 5, " ms");
	addSlider(props, key(prefix, "release_ms"), "Gate.Release", 0, 2000, 5, " ms");
	addSlider(props, key(prefix, "lookahead_ms"), "Gate.LookAhead", 0, maxLookaheadMs, 0.5, " ms");
}

} // namespace sarxina::gate
