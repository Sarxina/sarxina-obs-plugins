#include "eq-settings.hpp"

#include <algorithm>
#include <string>

namespace sarxina::eq {

namespace {

std::string key(int band, const char *what)
{
	return std::string(bandKey(band)) + "_" + what;
}

} // namespace

const char *bandKey(int band)
{
	static const char *const keys[BandCount] = {"hp", "ls", "b1", "b2", "b3", "b4", "b5", "hs", "lp"};
	return keys[band];
}

void setDefaults(obs_data_t *data)
{
	const Settings s = Settings::defaults();
	for (int band = 0; band < BandCount; band++) {
		const BandSettings &b = s.bands[band];
		obs_data_set_default_bool(data, key(band, "on").c_str(), b.on);
		obs_data_set_default_double(data, key(band, "hz").c_str(), b.hz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			obs_data_set_default_int(data, key(band, "slope").c_str(), b.slope);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			obs_data_set_default_double(data, key(band, "db").c_str(), b.db);
			obs_data_set_default_bool(data, key(band, "steep").c_str(), b.steep);
			break;
		case Kind::Bell:
			obs_data_set_default_double(data, key(band, "db").c_str(), b.db);
			obs_data_set_default_double(data, key(band, "q").c_str(), b.q);
			break;
		}
	}
	obs_data_set_default_double(data, "gain_db", s.gainDb);
	obs_data_set_default_bool(data, "wide_range", s.wideRange);
}

Settings read(obs_data_t *data)
{
	Settings s = Settings::defaults();
	for (int band = 0; band < BandCount; band++) {
		BandSettings &b = s.bands[band];
		b.on = obs_data_get_bool(data, key(band, "on").c_str());
		b.hz = std::clamp(obs_data_get_double(data, key(band, "hz").c_str()), minHz, maxHz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			b.slope = std::clamp(static_cast<int>(obs_data_get_int(data, key(band, "slope").c_str())) / 6 * 6, 6,
					     48);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			b.db = std::clamp(obs_data_get_double(data, key(band, "db").c_str()), -maxDb, maxDb);
			b.steep = obs_data_get_bool(data, key(band, "steep").c_str());
			break;
		case Kind::Bell:
			b.db = std::clamp(obs_data_get_double(data, key(band, "db").c_str()), -maxDb, maxDb);
			b.q = std::clamp(obs_data_get_double(data, key(band, "q").c_str()), minQ, maxQ);
			break;
		}
	}
	s.gainDb = std::clamp(obs_data_get_double(data, "gain_db"), -maxDb, maxDb);
	s.wideRange = obs_data_get_bool(data, "wide_range");
	return s;
}

void write(obs_data_t *data, const Settings &s)
{
	for (int band = 0; band < BandCount; band++) {
		const BandSettings &b = s.bands[band];
		obs_data_set_bool(data, key(band, "on").c_str(), b.on);
		obs_data_set_double(data, key(band, "hz").c_str(), b.hz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			obs_data_set_int(data, key(band, "slope").c_str(), b.slope);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			obs_data_set_double(data, key(band, "db").c_str(), b.db);
			obs_data_set_bool(data, key(band, "steep").c_str(), b.steep);
			break;
		case Kind::Bell:
			obs_data_set_double(data, key(band, "db").c_str(), b.db);
			obs_data_set_double(data, key(band, "q").c_str(), b.q);
			break;
		}
	}
	obs_data_set_double(data, "gain_db", s.gainDb);
	obs_data_set_bool(data, "wide_range", s.wideRange);
}

} // namespace sarxina::eq
