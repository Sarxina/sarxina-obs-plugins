# An audio filter, not a dock: it is built from its own sources only, without the shared dock code.
sarxina_add_obs_module(
  sarxina-noise-gate
  TITLE "Sarxina Noise Gate"
  VERSION 1.0.0
  DESCRIPTION "Sarxina Noise Gate audio filter for OBS Studio"
  SOURCES src/gate.cpp src/gate.hpp src/gate-settings.cpp src/gate-settings.hpp src/self-test.cpp
  MODULE_SOURCES src/plugin-main.cpp
)
