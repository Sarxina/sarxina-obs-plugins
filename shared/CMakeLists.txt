# The code every plugin here is built from: the panel, the dock, the link to OBS and the self-test helpers. Compiled
# once and linked into each plugin and each self-test program.

# The head picture, compiled in. Done here rather than by AUTORCC, which names the compiled file wrongly for the
# plugins that link an object library.
qt_add_resources(_resources src/resources.qrc)

add_library(
  sarxina-shared OBJECT
  src/control-panel.cpp
  src/control-panel.hpp
  src/controls-dock.cpp
  src/controls-dock.hpp
  src/controls.hpp
  src/game.hpp
  src/obs-bridge.cpp
  src/obs-bridge.hpp
  src/panel-colors.hpp
  src/panel-painter.cpp
  src/panel-painter.hpp
  src/physics.hpp
  src/self-test.cpp
  src/self-test.hpp
  ${_resources}
)
sarxina_target_settings(sarxina-shared)
target_include_directories(sarxina-shared PUBLIC src)
