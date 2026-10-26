sarxina_add_obs_plugin(
  plinkolive
  TITLE PlinkoLive
  VERSION 1.2.0
  DESCRIPTION "PlinkoLive dock for OBS Studio"
  SOURCES src/plinko-board.cpp src/plinko-board.hpp src/self-test.cpp
  LIBRARIES sarxina-plinko
)
