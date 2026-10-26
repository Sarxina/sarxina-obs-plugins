# The code the dock plugins are built from: the panel, the dock, the link to OBS and the self-test helpers. Compiled
# once and linked into each of them and each self-test program.

# The head picture, compiled in. Done here rather than by AUTORCC, which names the compiled file wrongly for the
# plugins that link an object library.
qt_add_resources(_resources src/resources.qrc)

add_library(
  sarxina-shared OBJECT
  src/control-panel.cpp
  src/control-panel.hpp
  src/controls-dock.cpp
  src/controls-dock.hpp
  src/controls.hpp
  src/game.hpp
  src/obs-bridge.cpp
  src/obs-bridge.hpp
  src/panel-colors.hpp
  src/panel-painter.cpp
  src/panel-painter.hpp
  src/physics.hpp
  src/self-test.cpp
  src/self-test.hpp
  ${_resources}
)
sarxina_target_settings(sarxina-shared)
target_include_directories(sarxina-shared PUBLIC src)

# The Plinko board, for the plugins that have one (PlinkoLive, PlinkoToggle): its pegs and slots, how a ball falls
# through them, and how they look.
add_library(sarxina-plinko OBJECT src/physics.hpp src/plinko.cpp src/plinko.hpp src/plinko-painter.cpp src/plinko-painter.hpp)
sarxina_target_settings(sarxina-plinko)
target_include_directories(sarxina-plinko PUBLIC src)

# What sets a plugin off from chat, a command or a channel point redeem, with its settings (PlinkoToggle,
# JobApplication); and chat's text made safe to show.
add_library(sarxina-chat-trigger OBJECT src/chat-trigger.cpp src/chat-trigger.hpp src/plain-text.cpp src/plain-text.hpp)
sarxina_target_settings(sarxina-chat-trigger)
target_include_directories(sarxina-chat-trigger PUBLIC src)

# Ways into a plugin without Twitch, for demos and tests: its own requests in obs-websocket, and chat made up from them
# (JobApplication, ChatSaga).
add_library(sarxina-test-route OBJECT src/test-route.cpp src/test-route.hpp)
sarxina_target_settings(sarxina-test-route)
target_include_directories(sarxina-test-route PUBLIC src)
