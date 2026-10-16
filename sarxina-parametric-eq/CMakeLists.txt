# An audio filter, not a dock: it is built from its own sources only, without the shared dock code.
sarxina_add_obs_module(
  sarxina-parametric-eq
  TITLE "Sarxina Parametric EQ"
  VERSION 1.0.0
  DESCRIPTION "Sarxina Parametric EQ audio filter for OBS Studio"
  SOURCES
    src/curve-editor.cpp
    src/curve-editor.hpp
    src/eq-design.cpp
    src/eq-design.hpp
    src/eq-settings.cpp
    src/eq-settings.hpp
    src/self-test.cpp
  MODULE_SOURCES src/plugin-main.cpp
)
