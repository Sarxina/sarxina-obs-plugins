// Sarxina Noise Gate as an OBS plugin: an audio filter (Filters > Audio Filters > + on any audio source). The
// look-ahead delays the source's sound by that long.
#include "gate-settings.hpp"
#include "gate.hpp"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QtGlobal>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return PLUGIN_TITLE;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

namespace {

using namespace sarxina::gate;

struct Filter {
	size_t channels = 2;
	std::unique_ptr<Processor> processor; // audio thread only

	// New settings wait here until the audio thread picks them up between two blocks.
	std::mutex lock;
	Settings pending;
	bool hasPending = false;
};

const char *filterName(void *)
{
	return obs_module_text("FilterName");
}

void filterUpdate(void *data, obs_data_t *settings)
{
	auto *f = static_cast<Filter *>(data);
	std::lock_guard<std::mutex> hold(f->lock);
	f->pending = read(settings);
	f->hasPending = true;
}

void *filterCreate(obs_data_t *settings, obs_source_t *)
{
	auto *f = new Filter();
	f->channels = audio_output_get_channels(obs_get_audio());
	f->processor = std::make_unique<Processor>(audio_output_get_sample_rate(obs_get_audio()));
	f->processor->setSettings(read(settings));
	return f;
}

void filterDestroy(void *data)
{
	delete static_cast<Filter *>(data);
}

obs_audio_data *filterAudio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<Filter *>(data);
	{
		// Never wait on the UI: if it is busy handing over settings, take them with the next block.
		std::unique_lock<std::mutex> hold(f->lock, std::try_to_lock);
		if (hold.owns_lock() && f->hasPending) {
			f->processor->setSettings(f->pending);
			f->hasPending = false;
		}
	}
	float *planes[maxChannels] = {};
	int count = 0;
	for (size_t c = 0; c < std::min<size_t>(f->channels, maxChannels) && audio->data[c]; c++)
		planes[count++] = reinterpret_cast<float *>(audio->data[c]);
	f->processor->process(planes, count, audio->frames);
	return audio;
}

void filterDefaults(obs_data_t *settings)
{
	setDefaults(settings);
}

void addSlider(obs_properties_t *props, const char *key, const char *text, double low, double high, double step,
	       const char *suffix)
{
	obs_property_t *p = obs_properties_add_float_slider(props, key, obs_module_text(text), low, high, step);
	obs_property_float_set_suffix(p, suffix);
	obs_property_set_long_description(p, obs_module_text((std::string(text) + ".Help").c_str()));
}

obs_properties_t *filterProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	addSlider(props, "open_db", "Open", -96, 0, 0.5, " dB");
	addSlider(props, "close_db", "Close", -96, 0, 0.5, " dB");
	addSlider(props, "range_db", "Range", -96, 0, 0.5, " dB");
	addSlider(props, "attack_ms", "Attack", 0, 100, 0.5, " ms");
	addSlider(props, "hold_ms", "Hold", 0, 1000, 5, " ms");
	addSlider(props, "release_ms", "Release", 0, 2000, 5, " ms");
	addSlider(props, "lookahead_ms", "LookAhead", 0, maxLookaheadMs, 0.5, " ms");
	return props;
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING &&
	    qEnvironmentVariableIntValue("SARXINA_NOISE_GATE_SELFTEST") != 0)
		runSelfTest(nullptr);
}

} // namespace

bool obs_module_load(void)
{
	obs_source_info info = {};
	info.id = "sarxina_noise_gate";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = filterName;
	info.create = filterCreate;
	info.destroy = filterDestroy;
	info.update = filterUpdate;
	info.filter_audio = filterAudio;
	info.get_defaults = filterDefaults;
	info.get_properties = filterProperties;
	obs_register_source(&info);

	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	blog(LOG_INFO, "[%s] %s loaded", PLUGIN_TITLE, PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
