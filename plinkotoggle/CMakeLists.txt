# A source, not a dock: the board goes in a scene. It is built from the shared Plinko board (sarxina-plinko), the shared
# chat trigger (sarxina-chat-trigger) and its own sources, without the shared dock code. Chat and VTube Studio come from
# the Sarxina OBS Plugin Manager.
sarxina_add_obs_module(
  plinkotoggle
  TITLE PlinkoToggle
  VERSION 1.0.0
  DESCRIPTION "PlinkoToggle source for OBS Studio"
  SOURCES
    src/resources.qrc
    src/self-test.cpp
    src/toggle-board.cpp
    src/toggle-board.hpp
    src/vts-toggles.cpp
    src/vts-toggles.hpp
  MODULE_SOURCES
    src/board-source.cpp
    src/board-source.hpp
    src/in-obs-check.cpp
    src/plinkotoggle.cpp
    src/plinkotoggle.hpp
    src/plugin-main.cpp
  LIBRARIES sarxina-plinko sarxina-chat-trigger
)
