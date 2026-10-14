// Every plugin here has a self-test: a check that needs no screen, for developers. It plays the plugin's game in
// practice mode and says in the log what happened; nothing in OBS is started or stopped. Given a folder, it also
// paints the dock into PNG files there.
//
// Inside OBS: start OBS with the environment variable <PLUGIN>_SELFTEST=1 (SLINGLIVE_SELFTEST=1, for example), and
// <PLUGIN>_SELFTEST_PICTURES=<folder> for the pictures. The results go to the OBS log once OBS has finished loading.
// Without OBS: run the <plugin>-selftest program built beside the plugin; ctest runs it.
#pragma once

#include "controls.hpp"

#include <QString>

#include <string>
#include <vector>

// Each plugin defines this. True when everything came out as expected.
bool runSelfTest(const char *picturesFolder);

namespace sarxina {

class ControlsDock;

namespace test {

// One line of results in the log: "[<plugin>] self-test: ...", a warning when `ok` is false.
void report(bool ok, const char *format, ...);

// The buttons' names as the panel shows them, for the log.
std::string names(const std::vector<Control> &controls);

// Stands in for OBS and counts what it is asked to do.
struct CountingHost : Host {
	int calls = 0;
	void press(Control, bool) override { calls++; }
};

// Paints docks into PNG files, each at 1x and 2x. With no folder it paints nothing.
class Pictures {
public:
	explicit Pictures(const char *folder);
	~Pictures(); // says in the log how many it wrote
	bool wanted() const { return !folder_.isEmpty(); }
	void save(ControlsDock &dock, const char *title);

private:
	QString folder_;
	int written_ = 0;
};

// Sizes a dock for its pictures (640 x 430 unless told otherwise) and lets half a second go by.
void settle(ControlsDock &dock, int width = 640, int height = 430);

} // namespace test
} // namespace sarxina
