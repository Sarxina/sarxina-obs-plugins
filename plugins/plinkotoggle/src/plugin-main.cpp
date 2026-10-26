// PlinkoToggle as an OBS plugin: the board as a source (board-source.hpp), registered when OBS loads the plugin so that
// scenes with one load, and the plugin itself (plinkotoggle.hpp), which hands itself to the Sarxina OBS Plugin Manager
// once every plugin has loaded.
//
// Threads: chat, VTube Studio and the settings are on OBS's UI thread, where the manager calls everything. Each
// board moves and paints on OBS's graphics thread. The UI thread hands the boards what to show and the balls to drop,
// and the boards hand back, through OBS's UI task queue, what each ball came down on.
#include "board-source.hpp"
#include "plinkotoggle.hpp"
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
void checkInObs(sarxina::plinkotoggle::PlinkoToggle &plugin);

namespace {

sarxina::plinkotoggle::PlinkoToggle *plugin = nullptr;

void landed(const sarxina::plinkotoggle::Landing &landing)
{
	if (plugin)
		plugin->landed(landing);
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (qEnvironmentVariableIntValue("PLINKOTOGGLE_SELFTEST") != 0) {
			runSelfTest(qgetenv("PLINKOTOGGLE_SELFTEST_PICTURES").constData());
			checkInObs(*plugin);
		}
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		plugin->shutDown();
	}
}

} // namespace

bool obs_module_load(void)
{
	sarxina::plinkotoggle::registerBoardSource();
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::plinkotoggle::PlinkoToggle();
	sarxina::plinkotoggle::setLandingHandler(landed);
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	plugin->start();
}

void obs_module_unload(void)
{
	sarxina::plinkotoggle::stopLandings();
	sarxina::plinkotoggle::releaseBoardGraphics();
	delete plugin;
	plugin = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
