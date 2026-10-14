cmake_minimum_required(VERSION 3.28...3.30)

project(sarxina-obs-plugins LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

enable_testing()

include(ObsDependencies)
include(ObsPlugin)

# The code the plugins share, then one folder per plugin. To add a plugin, add its folder here.
add_subdirectory(shared)
add_subdirectory(slinglive)
add_subdirectory(plinkolive)
