// ChatSaga as an OBS plugin: the source that shows the town (town-source.hpp), registered when OBS loads it, and the
// plugin itself (chatsaga.hpp), handed to the Sarxina OBS Plugin Manager once every plugin has loaded.
#include "chatsaga.hpp"
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

sarxina::chatsaga::ChatSaga *plugin = nullptr;

// A file or folder in the plugin's own config folder, as a whole path.
QString configFile(const char *name)
{
	BPtr<char> path = obs_module_config_path(name);
	BPtr<char> whole = path ? os_get_abs_path_ptr(path) : nullptr;
	return QString::fromUtf8(whole ? whole.Get() : "");
}

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
		if (qEnvironmentVariableIntValue("CHATSAGA_SELFTEST") != 0)
			runSelfTest(nullptr);
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
		plugin->shutDown();
	}
}

// The town in the manager's "Sarxina Source" menu, in OBS's Sources list.
void offerTownSource()
{
	const sarxina_source_offer offer{PLUGIN_NAME, sarxina::chatsaga::kTownSourceId, obs_module_text("SourceName")};
	if (!sarxina_obs_plugin_manager_offer_source(&offer))
		blog(LOG_INFO, "[%s] the manager does not list sources (1.3.0 or newer does)", PLUGIN_TITLE);
}

} // namespace

bool obs_module_load(void)
{
	sarxina::chatsaga::registerTownSource();
	return true;
}

void obs_module_post_load(void)
{
	plugin = new sarxina::chatsaga::ChatSaga(configFile("profiles.json"), configFile("avatars"));
	plugin->server().setPage(gameFolder());
	if (obs_frontend_get_main_window())
		obs_frontend_add_event_callback(frontendEvent, nullptr);
	plugin->start();
	sarxina::chatsaga::setMercenaryModeHandler([](bool on) {
		if (plugin)
			plugin->setMercenaries(on);
	});
	offerTownSource();
	sarxina::chatsaga::addTestRoute(*plugin);
}

void obs_module_unload(void)
{
	sarxina::chatsaga::setMercenaryModeHandler(nullptr);
	delete plugin;
	plugin = nullptr;
	blog(LOG_INFO, "[%s] unloaded", PLUGIN_TITLE);
}
