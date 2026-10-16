// The OBS side of a plugin: built into each one, with PLUGIN_NAME, PLUGIN_TITLE and PLUGIN_VERSION set by
// sarxina_add_obs_plugin. It puts the dock with the plugin's game (makeGame) in OBS's Docks menu.
//
// Once every plugin has loaded, the plugin hands itself to the Sarxina OBS Plugin Manager, which switches it on or off
// and shows its one setting, practice or real mode. Without the manager the plugin is simply on, and the mode is set
// from the gear in its dock, as before.
#include "controls-dock.hpp"
#include "sarxina-obs-plugin-manager.h"
#include "self-test.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QByteArray>
#include <QDockWidget>

#include <cstring>

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

sarxina::ControlsDock *dock = nullptr; // while it is in OBS, which owns it
bool loaded = false;                   // OBS has finished loading

void setEnabled(void *, bool enabled)
{
	if (enabled == (dock != nullptr))
		return;
	if (!enabled) {
		obs_frontend_remove_dock(PLUGIN_NAME); // deletes the dock
		dock = nullptr;
		blog(LOG_INFO, "[%s] off: dock removed", PLUGIN_TITLE);
		return;
	}

	auto *added = new sarxina::ControlsDock(sarxina::makeGame());
	// OBS takes the widget and lists the dock in its Docks menu.
	if (!obs_frontend_add_dock_by_id(PLUGIN_NAME, obs_module_text("DockTitle"), added)) {
		delete added;
		blog(LOG_WARNING, "[%s] OBS did not accept the dock", PLUGIN_TITLE);
		return;
	}
	dock = added;
	QObject::connect(dock, &sarxina::ControlsDock::modeChosen,
			 [] { sarxina_obs_plugin_manager_settings_changed(PLUGIN_NAME); });
	// Switched on while OBS is running: show it. At start-up OBS puts it back where it was.
	if (loaded)
		if (auto *frame = qobject_cast<QDockWidget *>(dock->parentWidget()))
			frame->show();
	blog(LOG_INFO, "[%s] on: dock added, %s mode", PLUGIN_TITLE, dock->practice() ? "practice" : "real");
}

obs_properties_t *properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_property_t *mode = obs_properties_add_list(properties, "mode", obs_module_text("Mode"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, obs_module_text("Menu.Real"), "real");
	obs_property_list_add_string(mode, obs_module_text("Menu.Practice"), "practice");
	return properties;
}

void getSettings(void *, obs_data_t *settings)
{
	obs_data_set_string(settings, "mode", sarxina::ControlsDock::savedPractice() ? "practice" : "real");
}

void update(void *, obs_data_t *settings)
{
	const bool practice = std::strcmp(obs_data_get_string(settings, "mode"), "practice") == 0;
	if (dock)
		dock->setPractice(practice);
	else
		sarxina::ControlsDock::savePractice(practice);
}

sarxina_plugin_info plugin = {
	SARXINA_OBS_PLUGIN_MANAGER_API,
	PLUGIN_NAME,
	PLUGIN_TITLE,
	nullptr, // the description, from the strings
	nullptr,
	setEnabled,
	properties,
	getSettings,
	update,
	nullptr, // no chat commands yet
	0,
	nullptr, // and no use for the rest of chat
};

// SLINGLIVE_SELFTEST, SLINGLIVE_SELFTEST_PICTURES and so on.
QByteArray environment(const char *suffix)
{
	return QByteArray(PLUGIN_NAME).toUpper() + suffix;
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	loaded = true;
	if (qEnvironmentVariableIntValue(environment("_SELFTEST").constData()) != 0)
		runSelfTest(qgetenv(environment("_SELFTEST_PICTURES")).constData());
}

} // namespace

bool obs_module_load(void)
{
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	return true;
}

void obs_module_post_load(void)
{
	if (!obs_frontend_get_main_window()) {
		blog(LOG_WARNING, "[%s] %s: OBS has no main window, so there is no dock", PLUGIN_TITLE, PLUGIN_VERSION);
		return;
	}
	plugin.description = obs_module_text("Description");
	if (sarxina_obs_plugin_manager_register(&plugin)) {
		blog(LOG_INFO, "[%s] %s loaded, run by the Sarxina OBS Plugin Manager", PLUGIN_TITLE, PLUGIN_VERSION);
	} else {
		blog(LOG_INFO, "[%s] %s loaded, on its own", PLUGIN_TITLE, PLUGIN_VERSION);
		setEnabled(nullptr, true);
	}
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
