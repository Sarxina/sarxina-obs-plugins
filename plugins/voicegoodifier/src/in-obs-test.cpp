#include "in-obs-test.hpp"

#include "filters.hpp"
#include "tests.hpp"

#include <obs-module.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::voice;
using sarxina::voice::test::Tally;

namespace {

constexpr double pi = 3.14159265358979323846;

// What came out of a filter: the loudest sample, and the level against the tone that went in.
struct Measured {
	double peakDb = 0, gainDb = 0;
};

// One filter made by OBS from settings, with a tone at a peak level through it and what should come out.
struct Case {
	const char *id;
	std::function<void(obs_data_t *)> settings; // what differs from the defaults
	double toneDb;
	std::function<bool(const Measured &)> expected;
	std::string expectation;
};

// Puts 1.5 s of a 1 kHz tone through a filter in 10 ms blocks on every channel OBS mixes, as OBS hands audio over,
// and measures the last half second.
Measured throughFilter(obs_source_t *filter, double toneDb)
{
	const size_t channels = audio_output_get_channels(obs_get_audio());
	const double rate = audio_output_get_sample_rate(obs_get_audio());
	const size_t block = size_t(rate / 100), total = size_t(rate * 1.5), measureFrom = size_t(rate);
	const double amplitude = std::pow(10, toneDb / 20);
	std::vector<std::vector<float>> planes(channels, std::vector<float>(block));
	double powerIn = 0, powerOut = 0, peak = 0;
	for (size_t at = 0; at < total; at += block) {
		obs_audio_data audio = {};
		audio.frames = uint32_t(block);
		for (size_t c = 0; c < channels; c++) {
			for (size_t i = 0; i < block; i++)
				planes[c][i] = float(amplitude * std::sin(2 * pi * 1000 * double(at + i) / rate));
			audio.data[c] = reinterpret_cast<uint8_t *>(planes[c].data());
		}
		runFilter(filter, &audio);
		if (at < measureFrom)
			continue;
		for (size_t i = 0; i < block; i++) {
			const double in = amplitude * std::sin(2 * pi * 1000 * double(at + i) / rate), out = planes[0][i];
			powerIn += in * in;
			powerOut += out * out;
			peak = std::max(peak, std::abs(out));
		}
	}
	return {20 * std::log10(std::max(peak, 1e-12)), 10 * std::log10(std::max(powerOut, 1e-24) / powerIn)};
}

// Every kind is in OBS's list of filters, under its own name, and only the replaced ids are hidden from the + menu.
bool listed()
{
	Tally t;
	size_t count = 0;
	const Kind *all = kinds(count);
	for (size_t k = 0; k < count; k++) {
		const char *id = nullptr;
		bool found = false;
		for (size_t i = 0; obs_enum_filter_types(i, &id); i++)
			found = found || std::strcmp(id, all[k].id) == 0;
		const char *name = obs_source_get_display_name(all[k].id);
		const bool hidden = (obs_get_source_output_flags(all[k].id) & OBS_SOURCE_DEPRECATED) != 0;
		t.expect(found && name && std::strcmp(name, obs_module_text(all[k].nameKey)) == 0,
			 std::string(all[k].id) + " is \"" + (name ? name : "missing") + "\"");
		t.expect(hidden == all[k].replaced, std::string(all[k].id) + (hidden ? " hidden" : " in the + menu"));
		blog(LOG_INFO, "[%s] self-test: in OBS: %s \"%s\"%s", obs_module_name(), all[k].id, name ? name : "missing",
		     hidden ? ", hidden from the + menu" : "");
	}
	return t.report("in OBS: the filters OBS lists");
}

// Makes the case's filter through OBS, puts its tone through it, and says what came out.
void runCase(const Case &c, Tally &t)
{
	obs_data_t *settings = obs_data_create();
	if (c.settings)
		c.settings(settings);
	obs_source_t *filter = obs_source_create_private(c.id, "VoiceGoodifier self-test", settings);
	obs_data_release(settings);
	if (!filter) {
		t.expect(false, std::string(c.id) + " could not be made");
		return;
	}
	const Measured m = throughFilter(filter, c.toneDb);
	obs_source_release(filter);
	const bool ok = c.expected(m);
	blog(ok ? LOG_INFO : LOG_WARNING, "[%s] self-test: in OBS: %s, a %.0f dB tone -> peak %.2f dB, gain %.2f dB (%s): %s",
	     obs_module_name(), c.id, c.toneDb, m.peakDb, m.gainDb, c.expectation.c_str(), ok ? "OK" : "FAILED");
	t.expect(ok, c.id);
}

std::vector<Case> cases()
{
	const double rate = audio_output_get_sample_rate(obs_get_audio());
	const double chainEqDb = eq::Design::of(eq::Settings::streamVoice(), rate).db(1000, rate);
	const double ceiling = limiter::Settings().maximumAmplitudeDb, range = gate::Settings().rangeDb;
	auto gainNear = [](double want) { return [want](const Measured &m) { return std::abs(m.gainDb - want) < 0.1; }; };
	auto peakUnder = [](double most) { return [most](const Measured &m) { return m.peakDb <= most + 0.01; }; };
	char chainRoom[80];
	std::snprintf(chainRoom, sizeof chainRoom, "expected gain %.2f dB, the range and the EQ", range + chainEqDb);
	return {
		{"voicegoodifier_gate", nullptr, -70, gainNear(range), "expected gain -30 dB, the range"},
		{"voicegoodifier_eq", nullptr, -20, gainNear(0), "expected gain 0 dB, Audition's flat EQ"},
		{"voicegoodifier_limiter", nullptr, 0, peakUnder(ceiling), "expected peak at most -1.5 dB"},
		{"voicegoodifier_chain", nullptr, -70, gainNear(range + chainEqDb), chainRoom},
		{"voicegoodifier_chain", nullptr, 0, peakUnder(ceiling), "expected peak at most -1.5 dB"},
		{"sarxina_noise_gate", [](obs_data_t *d) { obs_data_set_double(d, "range_db", -40); }, -70, gainNear(-40),
		 "range_db -40 as the old plugin saved it: expected gain -40 dB"},
		{"sarxina_parametric_eq", [](obs_data_t *d) { obs_data_set_double(d, "gain_db", -6); }, -20, gainNear(-6),
		 "gain_db -6 as the old plugin saved it: expected gain -6 dB"},
		{"sarxina_hard_limiter", [](obs_data_t *d) { obs_data_set_double(d, "maximum_amplitude_db", -6); }, 0,
		 peakUnder(-6), "maximum_amplitude_db -6 as the old plugin saved it: expected peak at most -6 dB"},
	};
}

bool processing()
{
	Tally t;
	for (const Case &c : cases())
		runCase(c, t);
	return t.report("in OBS: filters made by OBS, a tone through each");
}

} // namespace

bool sarxina::voice::test::inObsTest()
{
	return listed() & processing();
}
