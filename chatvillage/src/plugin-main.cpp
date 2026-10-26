// ChatVillage as an OBS plugin: the source that shows the town (town-source.hpp), registered when OBS loads it, and the
// plugin itself (chatvillage.hpp), handed to the Sarxina OBS Plugin Manager once every plugin has loaded.
#include "chatvillage.hpp"
#include "self-test.hpp"
#include "town-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

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

sarxina::chatvillage::ChatVillage *plugin = nullptr;

// The game's folder in the plugin's data, as a whole path: a portable OBS gives it relative to its own folder.
QString gameFolder()
{
	BPtr<char> folder = obs_module_file("game");
	BPtr<char> whole = folder ? os_get_abs_path_ptr(folder) : nullptr;
	return QString::fromUtf8(whole ? whole.Get() : "");
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (qEnvironmentVariableIntValue("CHATVILLAGE_SELFTEST") != 0)
			runSelfTest(nullptr);
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		plugin->shutDown();
	}
}

} // namespace

bool obs_module_load(void)
{
	sarxina::chatvillage::registerTownSource();
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::chatvillage::ChatVillage();
	plugin->server().setPage(gameFolder());
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	plugin->start();
	sarxina::chatvillage::addTestRoute(*plugin);
}

void obs_module_unload(void)
{
	delete plugin;
	plugin = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
