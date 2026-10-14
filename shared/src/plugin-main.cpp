// The OBS side of a plugin: built into each one, with PLUGIN_NAME, PLUGIN_TITLE and PLUGIN_VERSION set by
// sarxina_add_obs_plugin. It adds the dock with the plugin's game (makeGame) to OBS's Docks menu.
#include "controls-dock.hpp"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QByteArray>

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

// SLINGLIVE_SELFTEST, SLINGLIVE_SELFTEST_PICTURES and so on.
QByteArray environment(const char *suffix)
{
	return QByteArray(PLUGIN_NAME).toUpper() + suffix;
}

void selfTestWhenLoaded(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	obs_frontend_remove_event_callback(selfTestWhenLoaded, nullptr);
	runSelfTest(qgetenv(environment("_SELFTEST_PICTURES")).constData());
}

} // namespace

bool obs_module_load(void)
{
	if (!obs_frontend_get_main_window()) {
		blog(LOG_WARNING, "[%s] %s: OBS has no main window, so there is no dock", PLUGIN_TITLE, PLUGIN_VERSION);
		return true;
	}

	auto *dock = new sarxina::ControlsDock(sarxina::makeGame());
	const bool practice = dock->practice();
	// OBS takes the widget and lists the dock in its Docks menu.
	if (!obs_frontend_add_dock_by_id(PLUGIN_NAME, obs_module_text("DockTitle"), dock)) {
		delete dock;
		blog(LOG_WARNING, "[%s] %s: OBS did not accept the dock", PLUGIN_TITLE, PLUGIN_VERSION);
		return true;
	}
	blog(LOG_INFO, "[%s] %s loaded: dock added, %s mode", PLUGIN_TITLE, PLUGIN_VERSION,
	     practice ? "practice" : "real");

	if (qEnvironmentVariableIntValue(environment("_SELFTEST").constData()) != 0)
		obs_frontend_add_event_callback(selfTestWhenLoaded, nullptr);
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
