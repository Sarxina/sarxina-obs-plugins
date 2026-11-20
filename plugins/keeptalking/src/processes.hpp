// The programs running on this computer, for picking the game, and closing it.
//
// A game is known by its exe's file name ("Wow.exe"), so it is found again after it restarts. Closing it ends every
// process of that exe name in the streamer's own Windows session, and never OBS itself or a part of Windows.
#pragma once

#include <string>
#include <vector>

namespace sarxina::keeptalking {

struct Program {
	std::string exe;   // "Wow.exe"
	std::string title; // the title of one of its windows, "World of Warcraft"
};

// The programs with a window on screen, one line per exe, sorted by name; OBS and Windows' own left out.
std::vector<Program> programsWithWindows();

// Whether a process of that exe name runs in this session.
bool isRunning(const std::string &exe);

// False for OBS itself and the parts of Windows that are never closed.
bool mayClose(const std::string &exe);

struct Closed {
	int found = 0;  // processes of that exe name in this session
	int closed = 0; // of which ended
	std::string problem; // why one could not be ended, in Windows' words
};

// Ends every process of that exe name in this session, at once, the way Task Manager's End task does.
Closed closeProgram(const std::string &exe);

} // namespace sarxina::keeptalking
