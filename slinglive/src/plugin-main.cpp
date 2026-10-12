// SlingLive: an OBS Studio dock that stands in for the Controls panel. The buttons float in the air; to press one
// you pull a head back on a slingshot and let go.

#include "self-test.hpp"
#include "sling-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QString>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "SlingLive";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

static void selfTestWhenLoaded(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	obs_frontend_remove_event_callback(selfTestWhenLoaded, nullptr);
	runSelfTest(qEnvironmentVariable("SLINGLIVE_SELFTEST_PICTURES").toUtf8().constData());
}

bool obs_module_load(void)
{
	if (!obs_frontend_get_main_window()) {
		blog(LOG_WARNING, "[slinglive] %s: OBS has no main window, so there is no dock", PLUGIN_VERSION);
		return true;
	}

	auto *dock = new SlingDock();
	const bool practice = dock->practice();
	// OBS takes the widget and lists the dock in its Docks menu.
	if (!obs_frontend_add_dock_by_id("slinglive", obs_module_text("DockTitle"), dock)) {
		delete dock;
		blog(LOG_WARNING, "[slinglive] %s: OBS did not accept the dock", PLUGIN_VERSION);
		return true;
	}
	blog(LOG_INFO, "[slinglive] %s loaded: dock added, %s mode", PLUGIN_VERSION, practice ? "practice" : "real");

	if (selfTestRequested())
		obs_frontend_add_event_callback(selfTestWhenLoaded, nullptr);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[slinglive] unloaded");
}
