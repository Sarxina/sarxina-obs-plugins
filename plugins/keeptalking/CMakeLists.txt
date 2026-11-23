# A source and a watcher, not a dock: while the streamer's mic stays under a level for a few seconds, a countdown shows
# in its source, and at zero the game picked in the settings is closed. Built from its own sources and the shared test
# route (obs-websocket requests for a demo), without the shared dock code.
find_package(Qt6 REQUIRED COMPONENTS Network) # the leaderboard; OBS ships Qt6Network.dll
sarxina_add_obs_module(
  keeptalking
  TITLE KeepTalking
  VERSION 1.1.0
  DESCRIPTION "KeepTalking plugin for OBS Studio"
  SOURCES
    src/countdown-picture.cpp
    src/countdown-picture.hpp
    src/processes.cpp
    src/processes.hpp
    src/self-test.cpp
    src/silence-countdown.cpp
    src/silence-countdown.hpp
    src/word-counter.cpp
    src/word-counter.hpp
  MODULE_SOURCES
    src/countdown-source.cpp
    src/countdown-source.hpp
    src/keeptalking.cpp
    src/keeptalking.hpp
    src/leaderboard.cpp
    src/leaderboard.hpp
    src/plugin-main.cpp
    src/test-route.cpp
    src/word-count-source.cpp
    src/word-count-source.hpp
  LIBRARIES sarxina-test-route Qt6::Network winhttp
)

# The word counter over a WAV file, to check it against a transcript (not shipped).
add_executable(keeptalking-wordcount tools/wordcount.cpp src/word-counter.cpp src/word-counter.hpp)
target_include_directories(keeptalking-wordcount PRIVATE src)
sarxina_target_settings(keeptalking-wordcount)
