find_package(Qt6 REQUIRED COMPONENTS Network) # the town's local trigger and feed; OBS ships Qt6Network.dll

# A town in RPG Maker MZ where everyone in chat is an NPC. The plugin keeps the town and serves it on this computer;
# the game (game/, deployed into the plugin's data folder) draws it, in a source of the plugin's own. Chat and Twitch's
# API come from the Sarxina OBS Plugin Manager.
sarxina_add_obs_module(
  chatvillage
  TITLE ChatVillage
  VERSION 1.0.0
  DESCRIPTION "ChatVillage plugin for OBS Studio"
  SOURCES
    src/chat-presence.cpp
    src/chat-presence.hpp
    src/chatvillage.cpp
    src/chatvillage.hpp
    src/local-http.cpp
    src/local-http.hpp
    src/self-test.cpp
    src/town-page.cpp
    src/town-page.hpp
    src/town-server.cpp
    src/town-server.hpp
    src/town.cpp
    src/town.hpp
    src/twitch-chatters.cpp
    src/twitch-chatters.hpp
  MODULE_SOURCES
    src/plugin-main.cpp
    src/test-route.cpp
    src/town-source.cpp
    src/town-source.hpp
  LIBRARIES sarxina-chat-trigger sarxina-test-route Qt6::Network
)

# The game as players run it, beside the plugin's strings, so that it is installed and packaged with them.
add_custom_target(
  chatvillage-game
  COMMAND "${CMAKE_COMMAND}" "-DGAME=${CMAKE_CURRENT_SOURCE_DIR}/game" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/data/game"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/deploy-game.cmake"
  COMMENT "Deploying ChatVillage's game"
  VERBATIM
)
add_dependencies(chatvillage chatvillage-game)

# The town without OBS, for working on the game in a browser (town.ps1 starts it): the same page, trigger and feed,
# with no chat. It needs only Qt.
add_executable(
  chatvillage-town
  src/local-http.cpp
  src/town-main.cpp
  src/town-page.cpp
  src/town-server.cpp
  src/town.cpp
  "${SARXINA_SHARED_DIR}/src/plain-text.cpp"
)
target_include_directories(chatvillage-town PRIVATE "${SARXINA_SHARED_DIR}/src")
target_link_libraries(chatvillage-town PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(chatvillage-town PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(chatvillage-town PRIVATE /W4 /utf-8 /permissive- /Zc:__cplusplus)
set_target_properties(chatvillage-town PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
