// The countdown as an OBS source, offered under Sarxina Source: as big as the canvas, with START YAPPING OR
// ELSE over the number in its middle while it counts and nothing at all otherwise. Each new number pops in (118% to 100% over a quarter second), like
// Sarxina's captions. Every countdown source shows the same number, which the plugin hands over from the UI thread; the
// sources paint and draw it on OBS's graphics thread.
#pragma once

namespace sarxina::keeptalking {

inline constexpr const char *kSourceId = "keeptalking_countdown";

void registerCountdownSource();
// OBS is unloading the plugin.
void releaseCountdownGraphics();

// What every countdown source shows from now on: 5 to 0, or -1 for nothing.
void showNumber(int number);

} // namespace sarxina::keeptalking
