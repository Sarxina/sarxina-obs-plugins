// The Sarxina OBS Plugin Manager as an OBS plugin: its dock, "Sarxina Source" in the Sources list (source-menu.hpp),
// and the five procedures other plugins reach it through
// (sarxina-obs-plugin-manager.h). They go on OBS's global procedure handler when the manager loads, which is before
// any plugin's obs_module_post_load, so every plugin finds them whatever order OBS loads plugins in.
#include "manager-dock.hpp"
#include "manager.hpp"
#include "self-test.hpp"
#include "source-menu.hpp"

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

// The parts of the self-test that need OBS and the plugins in it (self-test.cpp).
void checkInObs(sarxina::Manager &manager);
void pressInObs(sarxina::ManagerDock &dock, sarxina::Manager &manager, const QString &press, const QString &pictures);

namespace {

sarxina::Manager *manager = nullptr;
sarxina::ManagerDock *dock = nullptr; // while it is in OBS, which owns it
sarxina::SourceMenu *sourceMenu = nullptr;

void registerPlugin(void *, calldata_t *call)
{
	const auto *info = static_cast<const sarxina_plugin_info *>(calldata_ptr(call, "plugin"));
	calldata_set_bool(call, "accepted", manager && manager->add(info));
}

void settingsChanged(void *, calldata_t *call)
{
	if (manager)
		manager->settingsChanged(calldata_string(call, "id"));
}

void vtsRequest(void *, calldata_t *call)
{
	const auto *request = static_cast<const sarxina_vts_request *>(calldata_ptr(call, "request"));
	if (manager && request)
		manager->vtsRequest(*request);
}

void twitchRequest(void *, calldata_t *call)
{
	const auto *request = static_cast<const sarxina_twitch_request *>(calldata_ptr(call, "request"));
	if (manager && request)
		manager->twitchRequest(*request);
}

void offerSource(void *, calldata_t *call)
{
	const auto *offer = static_cast<const sarxina_source_offer *>(calldata_ptr(call, "offer"));
	calldata_set_bool(call, "accepted", sourceMenu && offer && sourceMenu->offer(*offer));
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		manager->start();
		if (!sourceMenu->install(static_cast<QWidget *>(obs_frontend_get_main_window())))
			blog(LOG_WARNING, "[%s] no Sources list to add \"Sarxina Source\" to", PLUGIN_TITLE);
		if (qEnvironmentVariableIntValue("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST") != 0) {
			runSelfTest(qgetenv("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PICTURES").constData());
			checkInObs(*manager);
		}
		if (dock && qEnvironmentVariableIsSet("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PRESS"))
			pressInObs(*dock, *manager, qEnvironmentVariable("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PRESS"),
				   qEnvironmentVariable("SARXINA_OBS_PLUGIN_MANAGER_SELFTEST_PICTURES"));
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		dock = nullptr;
		manager->shutDown();
	}
}

} // namespace

bool obs_module_load(void)
{
	manager = new sarxina::Manager(sarxina::Manager::Use::Obs, sarxina::TwitchChat::kServer,
				       sarxina::Store::Setup::forObs(), sarxina::TwitchApi::Setup::forObs());
	proc_handler_t *procs = obs_get_proc_handler();
	proc_handler_add(procs, "void sarxina_obs_plugin_manager_register(in ptr plugin, out bool accepted)",
			 registerPlugin, nullptr);
	proc_handler_add(procs, "void sarxina_obs_plugin_manager_settings_changed(in string id)", settingsChanged,
			 nullptr);
	proc_handler_add(procs, "void sarxina_obs_plugin_manager_vts_request(in ptr request)", vtsRequest, nullptr);
	proc_handler_add(procs, "void sarxina_obs_plugin_manager_twitch_request(in ptr request)", twitchRequest, nullptr);
	sourceMenu = new sarxina::SourceMenu(*manager);
	proc_handler_add(procs, "void sarxina_obs_plugin_manager_offer_source(in ptr offer, out bool accepted)",
			 offerSource, nullptr);

	if (obs_frontend_get_main_window()) {
		dock = new sarxina::ManagerDock(*manager);
		// OBS takes the widget and lists the dock in its Docks menu.
		if (!obs_frontend_add_dock_by_id(PLUGIN_NAME, obs_module_text("DockTitle"), dock)) {
			delete dock;
			dock = nullptr;
			blog(LOG_WARNING, "[%s] OBS did not accept the dock", PLUGIN_TITLE);
		}
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	}
	blog(LOG_INFO, "[%s] %s loaded", PLUGIN_TITLE, PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	delete sourceMenu;
	sourceMenu = nullptr;
	delete manager;
	manager = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
