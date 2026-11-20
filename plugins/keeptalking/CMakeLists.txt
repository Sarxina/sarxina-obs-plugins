# A source and a watcher, not a dock: while the streamer's mic stays under a level for a few seconds, a countdown shows
# in its source, and at zero the game picked in the settings is closed. Built from its own sources and the shared test
# route (obs-websocket requests for a demo), without the shared dock code.
sarxina_add_obs_module(
  keeptalking
  TITLE KeepTalking
  VERSION 1.0.0
  DESCRIPTION "KeepTalking plugin for OBS Studio"
  SOURCES
    src/countdown-picture.cpp
    src/countdown-picture.hpp
    src/processes.cpp
    src/processes.hpp
    src/self-test.cpp
    src/silence-countdown.cpp
    src/silence-countdown.hpp
  MODULE_SOURCES
    src/countdown-source.cpp
    src/countdown-source.hpp
    src/keeptalking.cpp
    src/keeptalking.hpp
    src/plugin-main.cpp
    src/test-route.cpp
  LIBRARIES sarxina-test-route
)
