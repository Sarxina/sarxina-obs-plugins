// KeepTalking as an OBS plugin: the countdown source (countdown-source.hpp), registered when OBS loads the plugin so
// that scenes with one load, and the plugin itself (keeptalking.hpp), handed to the Sarxina OBS Plugin Manager once
// every plugin has loaded.
#include "countdown-source.hpp"
#include "keeptalking.hpp"
#include "sarxina-obs-plugin-manager.h"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

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

namespace sarxina::keeptalking {
void addTestRoute(KeepTalking &plugin);
}

namespace {

sarxina::keeptalking::KeepTalking *plugin = nullptr;

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (!plugin)
		return;
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		plugin->findMic();
		if (qEnvironmentVariableIntValue("KEEPTALKING_SELFTEST") != 0)
			runSelfTest(qgetenv("KEEPTALKING_SELFTEST_PICTURES").constData());
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		plugin->findMic();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		plugin->shutDown();
		break;
	default:
		break;
	}
}

// The countdown in the manager's "Sarxina Source" menu, in OBS's Sources list.
void offerCountdownSource()
{
	const sarxina_source_offer offer{PLUGIN_NAME, sarxina::keeptalking::kSourceId, obs_module_text("SourceName")};
	if (!sarxina_obs_plugin_manager_offer_source(&offer))
		blog(LOG_INFO, "[%s] the manager does not list sources (1.3.0 or newer does)", PLUGIN_TITLE);
}

} // namespace

bool obs_module_load(void)
{
	sarxina::keeptalking::registerCountdownSource();
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::keeptalking::KeepTalking();
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	plugin->start();
	offerCountdownSource();
	sarxina::keeptalking::addTestRoute(*plugin);
}

void obs_module_unload(void)
{
	delete plugin;
	plugin = nullptr;
	sarxina::keeptalking::releaseCountdownGraphics();
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
