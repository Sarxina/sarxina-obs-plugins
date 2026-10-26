cmake_minimum_required(VERSION 3.28...3.30)

project(sarxina-obs-plugins LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

enable_testing()

include(ObsDependencies)
include(ObsPlugin)

# The code the plugins share, then one folder per plugin (the Sarxina OBS Plugin Manager is one too). To add a
# plugin, add its folder here.
add_subdirectory(shared)
add_subdirectory(slinglive)
add_subdirectory(plinkolive)
add_subdirectory(plinkotoggle)
add_subdirectory(jobapplication)
add_subdirectory(chatsaga)
add_subdirectory(sarxina-obs-plugin-manager)
add_subdirectory(voicegoodifier)

# The smallest plugin the Sarxina OBS Plugin Manager can run, for anyone making their own. Never released.
add_subdirectory(examples/example-plugin)

# The list of plugins the Sarxina OBS Plugin Manager offers to install, every released plugin but the manager itself,
# and the manager's own release, which it updates itself from. The target plugins-index writes it into plugins.json
# (see BUILDING.md).
sarxina_add_plugins_index(
  MANAGER sarxina-obs-plugin-manager
  PLUGINS
  slinglive
  plinkolive
  plinkotoggle
  jobapplication
  chatsaga
  voicegoodifier
)
