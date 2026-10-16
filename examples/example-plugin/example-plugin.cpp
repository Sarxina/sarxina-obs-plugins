// The smallest plugin the Sarxina OBS Plugin Manager can run, for anyone making their own. Nothing in the manager
// knows about it: it is in the manager's list because it registers itself, and its gear shows the settings it
// describes here. Added, it answers "!hello" in chat (in the OBS log) and counts every chat message.
//
// It needs nothing but libobs and the one header, sarxina-obs-plugin-manager.h. BUILDING.md has the steps.
#include "sarxina-obs-plugin-manager.h"

#include <obs-module.h>
#include <util/platform.h>

#include <string>

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Example Plugin";
}

namespace {

// Its settings, which the plugin keeps itself, in its own config.json.
std::string greeting = "Hello";
long long times = 1;

bool on = false;
long long chatSeen = 0;

void save()
{
	char *folder = obs_module_config_path("");
	char *path = obs_module_config_path("config.json");
	os_mkdirs(folder);
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "greeting", greeting.c_str());
	obs_data_set_int(data, "times", times);
	obs_data_save_json_safe(data, path, "tmp", "bak");
	obs_data_release(data);
	bfree(path);
	bfree(folder);
}

void load()
{
	char *path = obs_module_config_path("config.json");
	if (obs_data_t *data = obs_data_create_from_json_file_safe(path, "bak")) {
		greeting = obs_data_get_string(data, "greeting");
		times = obs_data_get_int(data, "times");
		obs_data_release(data);
	}
	bfree(path);
}

// 1. On or off: off, it acts as if it were not installed.
void setEnabled(void *, bool enabled)
{
	on = enabled;
	blog(LOG_INFO, "[Example Plugin] %s", enabled ? "on" : "off");
}

// 2. Its settings, described the way OBS describes a source's settings. The manager draws them behind the gear.
obs_properties_t *properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_text(properties, "greeting", "Greeting", OBS_TEXT_DEFAULT);
	obs_properties_add_int(properties, "times", "Times to say it", 1, 3, 1);
	return properties;
}

void getSettings(void *, obs_data_t *settings)
{
	obs_data_set_string(settings, "greeting", greeting.c_str());
	obs_data_set_int(settings, "times", times);
}

void update(void *, obs_data_t *settings)
{
	greeting = obs_data_get_string(settings, "greeting");
	times = obs_data_get_int(settings, "times");
	save();
	blog(LOG_INFO, "[Example Plugin] settings: greeting \"%s\", %lld time(s)", greeting.c_str(), times);
}

// 3. The chat it wants: one command, and every message (only counted here).
void hello(void *, const char *args, const sarxina_chat_message *message)
{
	for (long long i = 0; i < times; i++)
		blog(LOG_INFO, "[Example Plugin] %s, %s! (%lld chat messages seen; after the command: \"%s\")",
		     greeting.c_str(), message->display_name, chatSeen, args);
}

void chat(void *, const sarxina_chat_message *)
{
	chatSeen++;
}

const char *const helloAliases[] = {"hi", nullptr};
const sarxina_chat_command commands[] = {
	{"hello", helloAliases, SARXINA_ROLE_EVERYONE, 10000, hello},
};

const sarxina_plugin_info info = {
	SARXINA_OBS_PLUGIN_MANAGER_API,
	"example-plugin",
	"Example Plugin",
	"The smallest plugin the Sarxina OBS Plugin Manager can run: it answers !hello in chat, in the OBS log.",
	nullptr,
	setEnabled,
	properties,
	getSettings,
	update,
	commands,
	1,
	chat,
};

} // namespace

bool obs_module_load(void)
{
	load();
	return true;
}

// 4. Hand itself to the manager once every plugin has loaded; with no manager, switch itself on.
void obs_module_post_load(void)
{
	if (!sarxina_obs_plugin_manager_register(&info))
		setEnabled(nullptr, true);
}
