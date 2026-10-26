#include "limiter-settings.hpp"

#include <obs-module.h>

#include <algorithm>
#include <string>

namespace sarxina::limiter {

namespace {

std::string key(const char *prefix, const char *name)
{
	return std::string(prefix) + name;
}

void addSlider(obs_properties_t *props, const std::string &key, const char *text, double low, double high,
	       double step, const char *suffix)
{
	obs_property_t *p = obs_properties_add_float_slider(props, key.c_str(), obs_module_text(text), low, high, step);
	obs_property_float_set_suffix(p, suffix);
}

} // namespace

void setDefaults(obs_data_t *data, const char *prefix)
{
	const Settings s;
	obs_data_set_default_double(data, key(prefix, "maximum_amplitude_db").c_str(), s.maximumAmplitudeDb);
	obs_data_set_default_double(data, key(prefix, "input_boost_db").c_str(), s.inputBoostDb);
	obs_data_set_default_double(data, key(prefix, "lookahead_ms").c_str(), s.lookaheadMs);
	obs_data_set_default_double(data, key(prefix, "release_ms").c_str(), s.releaseMs);
	obs_data_set_default_bool(data, key(prefix, "link_channels").c_str(), s.linkChannels);
}

Settings read(obs_data_t *data, const char *prefix)
{
	Settings s;
	s.maximumAmplitudeDb =
		std::clamp(obs_data_get_double(data, key(prefix, "maximum_amplitude_db").c_str()), -96.0, 0.0);
	s.inputBoostDb = std::clamp(obs_data_get_double(data, key(prefix, "input_boost_db").c_str()), -48.0, 48.0);
	s.lookaheadMs = std::clamp(obs_data_get_double(data, key(prefix, "lookahead_ms").c_str()), 0.0, maxLookaheadMs);
	s.releaseMs = std::clamp(obs_data_get_double(data, key(prefix, "release_ms").c_str()), 0.01, 10000.0);
	s.linkChannels = obs_data_get_bool(data, key(prefix, "link_channels").c_str());
	return s;
}

void write(obs_data_t *data, const Settings &s, const char *prefix)
{
	obs_data_set_double(data, key(prefix, "maximum_amplitude_db").c_str(), s.maximumAmplitudeDb);
	obs_data_set_double(data, key(prefix, "input_boost_db").c_str(), s.inputBoostDb);
	obs_data_set_double(data, key(prefix, "lookahead_ms").c_str(), s.lookaheadMs);
	obs_data_set_double(data, key(prefix, "release_ms").c_str(), s.releaseMs);
	obs_data_set_bool(data, key(prefix, "link_channels").c_str(), s.linkChannels);
}

void addProperties(obs_properties_t *props, const char *prefix)
{
	addSlider(props, key(prefix, "maximum_amplitude_db"), "Limiter.MaximumAmplitude", -40, 0, 0.1, " dB");
	addSlider(props, key(prefix, "input_boost_db"), "Limiter.InputBoost", -20, 40, 0.1, " dB");
	addSlider(props, key(prefix, "lookahead_ms"), "Limiter.LookAhead", 0, maxLookaheadMs, 0.1, " ms");
	addSlider(props, key(prefix, "release_ms"), "Limiter.Release", 1, 1000, 1, " ms");
	obs_properties_add_bool(props, key(prefix, "link_channels").c_str(), obs_module_text("Limiter.LinkChannels"));
}

} // namespace sarxina::limiter
