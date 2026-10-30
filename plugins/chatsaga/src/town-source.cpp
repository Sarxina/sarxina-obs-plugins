#include "town-source.hpp"
#include "town-server.hpp"

#include <obs-module.h>

#include <set>
#include <string>

namespace sarxina::chatsaga {

namespace {

constexpr const char *kMercenaries = "mercenaries"; // the setting for Mercenary mode

struct TownSource {
	obs_source_t *self;
	obs_source_t *game; // null when OBS has no browser source
};

std::function<void(bool)> mercenaryModeChanged;
std::set<TownSource *> withMercenaries; // the sources with Mercenary mode ticked

// Notes whether this source has Mercenary mode ticked, and tells the plugin whether any source has.
void setMercenaries(TownSource *town, bool on)
{
	const bool before = !withMercenaries.empty();
	if (on)
		withMercenaries.insert(town);
	else
		withMercenaries.erase(town);
	if (mercenaryModeChanged && before != !withMercenaries.empty())
		mercenaryModeChanged(!withMercenaries.empty());
}

// The browser source's settings for the game: the page as the town serves it (town-server.hpp), at the game's size,
// with its sound kept inside OBS. Served, not opened as a file: OBS 33's browser keeps a page opened as a file from
// reading the town's feed on 127.0.0.1, and the game would show nobody.
obs_data_t *gameSettings()
{
	obs_data_t *settings = obs_data_create();
	const std::string page = "http://127.0.0.1:" + std::to_string(kPort) + "/";
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_string(settings, "url", page.c_str());
	obs_data_set_int(settings, "width", kGameWidth);
	obs_data_set_int(settings, "height", kGameHeight);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	obs_data_set_string(settings, "css", "");
	return settings;
}

void *create(obs_data_t *sourceSettings, obs_source_t *source)
{
	auto *town = new TownSource{source, nullptr};
	setMercenaries(town, obs_data_get_bool(sourceSettings, kMercenaries));
	obs_data_t *settings = gameSettings();
	town->game = obs_source_create_private("browser_source", "ChatSaga game", settings);
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
	setMercenaries(town, false);
	if (town->game) {
		obs_source_remove_active_child(town->self, town->game);
		obs_source_release(town->game);
	}
	delete town;
}

obs_properties_t *properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, kMercenaries, obs_module_text("MercenaryMode"));
	return properties;
}

void update(void *data, obs_data_t *settings)
{
	setMercenaries(static_cast<TownSource *>(data), obs_data_get_bool(settings, kMercenaries));
}

// Interact passes the keyboard and mouse on to the game.
void mouseClick(void *data, const obs_mouse_event *event, int32_t type, bool up, uint32_t clicks)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_send_mouse_click(town->game, event, type, up, clicks);
}

void mouseMove(void *data, const obs_mouse_event *event, bool leaving)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_send_mouse_move(town->game, event, leaving);
}

void mouseWheel(void *data, const obs_mouse_event *event, int x, int y)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_send_mouse_wheel(town->game, event, x, y);
}

void focus(void *data, bool focused)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_send_focus(town->game, focused);
}

void keyClick(void *data, const obs_key_event *event, bool up)
{
	auto *town = static_cast<TownSource *>(data);
	if (town->game)
		obs_source_send_key_click(town->game, event, up);
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
	info.id = kTownSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION;
	info.icon_type = OBS_ICON_TYPE_GAME_CAPTURE;
	info.get_name = [](void *) { return obs_module_text("SourceName"); };
	info.create = create;
	info.destroy = destroy;
	info.get_width = [](void *) { return static_cast<uint32_t>(kGameWidth); };
	info.get_height = [](void *) { return static_cast<uint32_t>(kGameHeight); };
	info.video_render = render;
	info.enum_active_sources = enumActive;
	info.get_properties = properties;
	info.update = update;
	info.mouse_click = mouseClick;
	info.mouse_move = mouseMove;
	info.mouse_wheel = mouseWheel;
	info.focus = focus;
	info.key_click = keyClick;
	obs_register_source(&info);
}

void setMercenaryModeHandler(std::function<void(bool)> handler)
{
	mercenaryModeChanged = std::move(handler);
	if (mercenaryModeChanged)
		mercenaryModeChanged(!withMercenaries.empty());
}

} // namespace sarxina::chatsaga
