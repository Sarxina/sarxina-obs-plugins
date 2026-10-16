# An audio filter, not a dock: it is built from its own sources only, without the shared dock code.
sarxina_add_obs_module(
  sarxina-hard-limiter
  TITLE "Sarxina Hard Limiter"
  VERSION 1.0.0
  DESCRIPTION "Sarxina Hard Limiter audio filter for OBS Studio"
  SOURCES src/limiter.cpp src/limiter.hpp src/limiter-settings.cpp src/limiter-settings.hpp src/self-test.cpp
  MODULE_SOURCES src/plugin-main.cpp
)
