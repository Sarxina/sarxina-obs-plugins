// The town in a scene: the source "ChatSaga" shows ChatSaga's game, which draws the town from the plugin's feed
// (town-server.hpp). The game is a web page made with RPG Maker MZ, so the source holds a browser source of its own,
// in no scene, that loads the game from the plugin's data folder at the game's size; the game runs while the source is
// shown. Its sound stays inside OBS, and this source plays none. Interact (right-click the source) passes the keyboard
// and mouse to the game, and the source's properties have Mercenary mode.
#pragma once

#include <functional>

namespace sarxina::chatsaga {

inline constexpr const char *kTownSourceId = "chatsaga_town";
inline constexpr int kGameWidth = 1920;
inline constexpr int kGameHeight = 1080;

// Called from obs_module_load.
void registerTownSource();

// Told whether any ChatSaga source has Mercenary mode ticked, each time that changes.
void setMercenaryModeHandler(std::function<void(bool)> handler);

} // namespace sarxina::chatsaga
