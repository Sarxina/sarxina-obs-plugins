// The board as an OBS source: Sources > + > PlinkoToggle. Each one has its own board (toggle-board.hpp), which moves
// on OBS's graphics thread and is drawn wherever its scene is: the still picture, uploaded only when it changes, with
// the heads, name tags and flashing buttons drawn over it as small pictures of their own. The source is as big as its
// board, which grows with the toggles. All boards show the same thing, which the plugin hands them from the UI thread,
// and a ball drops only on the boards in the scene being streamed or recorded (a source OBS calls active), never on
// one that is only in the preview or in another scene.
//
// Everything here is called on the UI thread, and the landing handler runs there too.
#pragma once

#include "toggle-board.hpp"

#include <cstdint>
#include <optional>

namespace sarxina::plinkotoggle {

inline constexpr const char *kSourceId = "plinkotoggle";

void registerBoardSource();
// OBS is unloading the plugin.
void releaseBoardGraphics();

// What every board shows from now on, and every new one.
void showOnBoards(const Content &content);
Content shownOnBoards();
int boardCount();
// Whether a board is on screen, in the scene being streamed or recorded.
bool boardOnScreen();
// Drops a ball for `who` on every board on screen, and says on how many. A seed makes it fall a known way.
int dropOnBoards(const QString &who, std::optional<uint32_t> seed = std::nullopt);

// Where the balls that came down in a slot go, on the UI thread.
void setLandingHandler(void (*handler)(const Landing &landing));
// OBS is closing: nothing more is handed over.
void stopLandings();

} // namespace sarxina::plinkotoggle
