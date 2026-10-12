set(
  _slinglive_sources
  src/obs-bridge.cpp
  src/obs-bridge.hpp
  src/resources.qrc
  src/self-test.cpp
  src/self-test.hpp
  src/sling-dock.cpp
  src/sling-dock.hpp
  src/sling-game.cpp
  src/sling-game.hpp
)

sarxina_add_obs_plugin(
  slinglive
  VERSION 1.0.0
  DESCRIPTION "SlingLive dock for OBS Studio"
  DATA "${CMAKE_CURRENT_SOURCE_DIR}/data"
  SOURCES src/plugin-main.cpp ${_slinglive_sources}
)

# The same self-test the plugin can run inside OBS, as a program that needs no OBS and shows nothing on screen:
#   ctest --test-dir build_x64 -C RelWithDebInfo -V
sarxina_add_plugin_check(
  slinglive-selftest
  SOURCES tests/selftest-main.cpp ${_slinglive_sources}
  ARGUMENTS "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini" "${CMAKE_BINARY_DIR}/slinglive-pictures"
)
