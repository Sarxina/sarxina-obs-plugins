// JobApplication as an OBS plugin: no source and no dock of its own, since the paper is drawn by VTube Studio, on the
// model. The plugin (jobapplication.hpp) hands itself to the Sarxina OBS Plugin Manager once every plugin has loaded,
// registers its hotkey, and saves with OBS.
#include "jobapplication.hpp"
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

// The part of the self-test that needs OBS, the manager and VTube Studio (in-obs-check.cpp).
void checkInObs(sarxina::jobapplication::JobApplication &plugin);

namespace {

sarxina::jobapplication::JobApplication *plugin = nullptr;

void onSave(obs_data_t *, bool saving, void *)
{
	if (saving && plugin)
		plugin->save();
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (qEnvironmentVariableIntValue("JOBAPPLICATION_SELFTEST") != 0) {
			runSelfTest(qgetenv("JOBAPPLICATION_SELFTEST_PICTURES").constData());
			checkInObs(*plugin);
		}
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		obs_frontend_remove_save_callback(onSave, nullptr);
		plugin->shutDown();
	}
}

} // namespace

bool obs_module_load(void)
{
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::jobapplication::JobApplication();
	if (obs_frontend_get_main_window()) {
		obs_frontend_add_event_callback(frontendEvent, nullptr);
		obs_frontend_add_save_callback(onSave, nullptr);
	}
	plugin->start();
}

void obs_module_unload(void)
{
	delete plugin;
	plugin = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
