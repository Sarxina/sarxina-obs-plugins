#include "limiter-settings.hpp"

#include <algorithm>

namespace sarxina::limiter {

void setDefaults(obs_data_t *data)
{
	const Settings s;
	obs_data_set_default_double(data, "maximum_amplitude_db", s.maximumAmplitudeDb);
	obs_data_set_default_double(data, "input_boost_db", s.inputBoostDb);
	obs_data_set_default_double(data, "lookahead_ms", s.lookaheadMs);
	obs_data_set_default_double(data, "release_ms", s.releaseMs);
	obs_data_set_default_bool(data, "link_channels", s.linkChannels);
}

Settings read(obs_data_t *data)
{
	Settings s;
	s.maximumAmplitudeDb = std::clamp(obs_data_get_double(data, "maximum_amplitude_db"), -96.0, 0.0);
	s.inputBoostDb = std::clamp(obs_data_get_double(data, "input_boost_db"), -48.0, 48.0);
	s.lookaheadMs = std::clamp(obs_data_get_double(data, "lookahead_ms"), 0.0, maxLookaheadMs);
	s.releaseMs = std::clamp(obs_data_get_double(data, "release_ms"), 0.01, 10000.0);
	s.linkChannels = obs_data_get_bool(data, "link_channels");
	return s;
}

void write(obs_data_t *data, const Settings &s)
{
	obs_data_set_double(data, "maximum_amplitude_db", s.maximumAmplitudeDb);
	obs_data_set_double(data, "input_boost_db", s.inputBoostDb);
	obs_data_set_double(data, "lookahead_ms", s.lookaheadMs);
	obs_data_set_double(data, "release_ms", s.releaseMs);
	obs_data_set_bool(data, "link_channels", s.linkChannels);
}

} // namespace sarxina::limiter
