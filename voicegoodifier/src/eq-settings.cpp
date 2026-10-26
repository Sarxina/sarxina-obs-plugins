#include "eq-settings.hpp"

#include <algorithm>
#include <string>

namespace sarxina::eq {

namespace {

std::string key(const char *prefix, int band, const char *what)
{
	return std::string(prefix) + bandKey(band) + "_" + what;
}

std::string key(const char *prefix, const char *name)
{
	return std::string(prefix) + name;
}

} // namespace

const char *bandKey(int band)
{
	static const char *const keys[BandCount] = {"hp", "ls", "b1", "b2", "b3", "b4", "b5", "hs", "lp"};
	return keys[band];
}

void setDefaults(obs_data_t *data, const char *prefix, const Settings &s)
{
	for (int band = 0; band < BandCount; band++) {
		const BandSettings &b = s.bands[band];
		obs_data_set_default_bool(data, key(prefix, band, "on").c_str(), b.on);
		obs_data_set_default_double(data, key(prefix, band, "hz").c_str(), b.hz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			obs_data_set_default_int(data, key(prefix, band, "slope").c_str(), b.slope);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			obs_data_set_default_double(data, key(prefix, band, "db").c_str(), b.db);
			obs_data_set_default_bool(data, key(prefix, band, "steep").c_str(), b.steep);
			break;
		case Kind::Bell:
			obs_data_set_default_double(data, key(prefix, band, "db").c_str(), b.db);
			obs_data_set_default_double(data, key(prefix, band, "q").c_str(), b.q);
			obs_data_set_default_double(data, key(prefix, band, "width").c_str(), b.width);
			break;
		}
	}
	obs_data_set_default_double(data, key(prefix, "gain_db").c_str(), s.gainDb);
	obs_data_set_default_bool(data, key(prefix, "constant_width").c_str(), s.constantWidth);
	obs_data_set_default_bool(data, key(prefix, "wide_range").c_str(), s.wideRange);
}

Settings read(obs_data_t *data, const char *prefix)
{
	Settings s = Settings::defaults();
	for (int band = 0; band < BandCount; band++) {
		BandSettings &b = s.bands[band];
		b.on = obs_data_get_bool(data, key(prefix, band, "on").c_str());
		b.hz = std::clamp(obs_data_get_double(data, key(prefix, band, "hz").c_str()), minHz, maxHz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			b.slope = static_cast<int>(obs_data_get_int(data, key(prefix, band, "slope").c_str()));
			b.slope = std::clamp(b.slope / 6 * 6, 6, 48);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			b.db = std::clamp(obs_data_get_double(data, key(prefix, band, "db").c_str()), -maxDb, maxDb);
			b.steep = obs_data_get_bool(data, key(prefix, band, "steep").c_str());
			break;
		case Kind::Bell:
			b.db = std::clamp(obs_data_get_double(data, key(prefix, band, "db").c_str()), -maxDb, maxDb);
			b.q = std::clamp(obs_data_get_double(data, key(prefix, band, "q").c_str()), minQ, maxQ);
			b.width = std::clamp(obs_data_get_double(data, key(prefix, band, "width").c_str()), minWidth, maxWidth);
			break;
		}
	}
	s.gainDb = std::clamp(obs_data_get_double(data, key(prefix, "gain_db").c_str()), -maxDb, maxDb);
	s.constantWidth = obs_data_get_bool(data, key(prefix, "constant_width").c_str());
	s.wideRange = obs_data_get_bool(data, key(prefix, "wide_range").c_str());
	return s;
}

void write(obs_data_t *data, const Settings &s, const char *prefix)
{
	for (int band = 0; band < BandCount; band++) {
		const BandSettings &b = s.bands[band];
		obs_data_set_bool(data, key(prefix, band, "on").c_str(), b.on);
		obs_data_set_double(data, key(prefix, band, "hz").c_str(), b.hz);
		switch (kindOf(band)) {
		case Kind::HighPass:
		case Kind::LowPass:
			obs_data_set_int(data, key(prefix, band, "slope").c_str(), b.slope);
			break;
		case Kind::LowShelf:
		case Kind::HighShelf:
			obs_data_set_double(data, key(prefix, band, "db").c_str(), b.db);
			obs_data_set_bool(data, key(prefix, band, "steep").c_str(), b.steep);
			break;
		case Kind::Bell:
			obs_data_set_double(data, key(prefix, band, "db").c_str(), b.db);
			obs_data_set_double(data, key(prefix, band, "q").c_str(), b.q);
			obs_data_set_double(data, key(prefix, band, "width").c_str(), b.width);
			break;
		}
	}
	obs_data_set_double(data, key(prefix, "gain_db").c_str(), s.gainDb);
	obs_data_set_bool(data, key(prefix, "constant_width").c_str(), s.constantWidth);
	obs_data_set_bool(data, key(prefix, "wide_range").c_str(), s.wideRange);
}

} // namespace sarxina::eq
