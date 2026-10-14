// A check that needs no screen, for developers. It plays a few scripted flings through the game in practice mode
// and reports which buttons each one pressed, then drops the head into the Plinko board from all across the panel
// and reports where it came down; nothing in OBS is started or stopped. Given a folder, it also paints the dock
// into PNG files there.
//
// Inside OBS: start OBS with the environment variable SLINGLIVE_SELFTEST=1 (and, for the pictures,
// SLINGLIVE_SELFTEST_PICTURES=<folder>). The results go to the OBS log once OBS has finished loading.
// Without OBS: run the slinglive-selftest program built beside the plugin.
#pragma once

bool selfTestRequested();
// Returns true when every fling pressed the buttons it should and every drop came down in one slot.
bool runSelfTest(const char *picturesFolder);
