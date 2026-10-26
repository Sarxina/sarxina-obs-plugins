# Audio filters, not a dock: built from their own sources only, without the shared dock code. VoiceGoodifier took the
# place of Sarxina Parametric EQ, Noise Gate and Hard Limiter, and keeps their filter ids (see src/filters.hpp).
sarxina_add_obs_module(
  voicegoodifier
  TITLE "VoiceGoodifier"
  VERSION 1.0.0
  DESCRIPTION "VoiceGoodifier voice filters for OBS Studio"
  SOURCES
    src/curve-editor.cpp
    src/curve-editor.hpp
    src/eq-design.cpp
    src/eq-design.hpp
    src/eq-settings.cpp
    src/eq-settings.hpp
    src/gate.cpp
    src/gate.hpp
    src/gate-settings.cpp
    src/gate-settings.hpp
    src/limiter.cpp
    src/limiter.hpp
    src/limiter-settings.cpp
    src/limiter-settings.hpp
    src/voice-chain.cpp
    src/voice-chain.hpp
    src/tests.hpp
    src/eq-test.cpp
    src/gate-test.cpp
    src/limiter-test.cpp
    src/chain-test.cpp
    src/self-test.cpp
  MODULE_SOURCES src/filters.cpp src/filters.hpp src/in-obs-test.cpp src/in-obs-test.hpp src/plugin-main.cpp
)
