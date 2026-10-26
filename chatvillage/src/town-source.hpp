// The town in a scene: the source "ChatVillage" shows ChatVillage's game, which draws the town from the plugin's feed
// (town-server.hpp). The game is a web page made with RPG Maker MZ, so the source holds a browser source of its own,
// in no scene, that loads the game from the plugin's data folder at the game's size; the game runs while the source is
// shown. Its sound stays inside OBS, and this source plays none.
#pragma once

namespace sarxina::chatvillage {

inline constexpr int kGameWidth = 1920;
inline constexpr int kGameHeight = 1080;

// Called from obs_module_load.
void registerTownSource();

} // namespace sarxina::chatvillage
