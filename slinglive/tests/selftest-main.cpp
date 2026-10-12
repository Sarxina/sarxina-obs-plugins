// Runs SlingLive's self-test without OBS: the scripted flings, and pictures of the dock painted into PNG files.
// Nothing is shown on screen.
//
//   slinglive-selftest <en-US.ini> [folder for pictures]

#include "../src/self-test.hpp"

#include <obs-module.h>
#include <util/text-lookup.h>

#include <QApplication>

#include <cstdio>

static lookup_t *strings = nullptr;

// OBS gives a loaded plugin these two. Here there is no OBS, so the program stands in for it.
extern "C" const char *obs_module_text(const char *key)
{
	const char *words = key;
	if (strings)
		text_lookup_getstr(strings, key, &words);
	return words;
}

extern "C" obs_module_t *obs_current_module(void)
{
	return nullptr;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	if (argc < 2) {
		std::fprintf(stderr, "usage: slinglive-selftest <en-US.ini> [folder for pictures]\n");
		return 2;
	}

	strings = text_lookup_create(argv[1]);
	if (!strings) {
		std::fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	const bool passed = runSelfTest(argc > 2 ? argv[2] : nullptr);
	text_lookup_destroy(strings);
	return passed ? 0 : 1;
}
