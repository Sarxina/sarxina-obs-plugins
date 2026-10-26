#include "town-source.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

namespace sarxina::chatvillage {

namespace {

constexpr const char *kSourceId = "chatvillage_town";

struct TownSource {
	obs_source_t *self;
	obs_source_t *game; // null when OBS has no browser source
};

// The browser source's settings for the game: the page from the plugin's data folder, at the game's size, with its
// sound kept inside OBS.
obs_data_t *gameSettings()
{
	obs_data_t *settings = obs_data_create();
	BPtr<char> file = obs_module_file("game/index.html"); // relative to OBS's folder in a portable OBS
	BPtr<char> page = file ? os_get_abs_path_ptr(file) : nullptr;
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", page ? page.Get() : "");
	obs_data_set_int(settings, "width", kGameWidth);
	obs_data_set_int(settings, "height", kGameHeight);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_string(settings, "css", "");
	return settings;
}

void *create(obs_data_t *, obs_source_t *source)
{
	auto *town = new TownSource{source, nullptr};
	obs_data_t *settings = gameSettings();
	town->game = obs_source_create_private("browser_source", "ChatVillage game", settings);
	obs_data_release(settings);
	if (town->game)
		obs_source_add_active_child(source, town->game);
	else
		blog(LOG_WARNING, "[%s] the town cannot be shown: OBS has no browser source", PLUGIN_TITLE);
	return town;
}

void destroy(void *data)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game) {
		obs_source_remove_active_child(town->self, town->game);
		obs_source_release(town->game);
	}
	delete town;
}

void render(void *data, gs_effect_t *)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_video_render(town->game);
}

// The game is shown and runs whenever the source is.
void enumActive(void *data, obs_source_enum_proc_t callback, void *param)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		callback(town->self, town->game, param);
}

} // namespace

void registerTownSource()
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
	info.get_name = [](void *) { return obs_module_text("SourceName"); };
	info.create = create;
	info.destroy = destroy;
	info.get_width = [](void *) { return static_cast<uint32_t>(kGameWidth); };
	info.get_height = [](void *) { return static_cast<uint32_t>(kGameHeight); };
	info.video_render = render;
	info.enum_active_sources = enumActive;
	obs_register_source(&info);
}

} // namespace sarxina::chatvillage
