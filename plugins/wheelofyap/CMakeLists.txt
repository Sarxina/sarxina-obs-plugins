# A source, not a dock: the wheel goes in a scene. Chat and Twitch come from the Sarxina OBS Plugin Manager, through the
# shared chat trigger (two of them: a spin, and a forced topic).
sarxina_add_obs_module(
  wheelofyap
  TITLE WheelOfYap
  VERSION 1.0.0
  DESCRIPTION "WheelOfYap source for OBS Studio"
  SOURCES
    src/self-test.cpp
    src/topics-dialog.cpp
    src/topics-dialog.hpp
    src/wheel.cpp
    src/wheel.hpp
  MODULE_SOURCES
    src/plugin-main.cpp
    src/test-route.cpp
    src/wheel-source.cpp
    src/wheel-source.hpp
    src/wheelofyap.cpp
    src/wheelofyap.hpp
  LIBRARIES sarxina-chat-trigger sarxina-test-route
)

# The topics a new wheel starts with, beside the plugin's strings, so that they are installed and packaged with them.
configure_file(data/topics.txt "${CMAKE_CURRENT_BINARY_DIR}/data/topics.txt" COPYONLY)
# The self-test paints a wheel of them.
target_compile_definitions(wheelofyap-selftest PRIVATE WHEELOFYAP_TOPICS="${CMAKE_CURRENT_SOURCE_DIR}/data/topics.txt")
