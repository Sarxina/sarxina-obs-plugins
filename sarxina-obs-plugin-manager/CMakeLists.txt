find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED) # zlib.dll comes with OBS too, to unpack the plugins' zips
find_package(Qt6 REQUIRED COMPONENTS Network) # for the self-test's stand-in servers; OBS ships Qt6Network.dll

# The Client ID of the manager's app on dev.twitch.tv ("Sarxina OBS Plugin Manager", Client Type: Public), for its
# channel point connection. A public app has no secret, so the id is all there is, and it ships in the manager. Set
# empty, the dock says that this build cannot connect channel points.
set(SARXINA_TWITCH_CLIENT_ID "hvngbutjgwxcc49esrjkgb85o4osys" CACHE STRING
    "Client ID of the Sarxina OBS Plugin Manager's app on dev.twitch.tv")
if(NOT SARXINA_TWITCH_CLIENT_ID MATCHES "^[A-Za-z0-9]*$")
  message(FATAL_ERROR "SARXINA_TWITCH_CLIENT_ID has to be the Client ID from dev.twitch.tv: only letters and digits")
endif()

sarxina_add_obs_module(
  sarxina-obs-plugin-manager
  TITLE "Sarxina OBS Plugin Manager"
  VERSION 1.1.0
  DESCRIPTION "Sarxina OBS Plugin Manager dock for OBS Studio"
  SOURCES
    src/catalog.cpp
    src/catalog.hpp
    src/chat.hpp
    src/chat-router.cpp
    src/chat-router.hpp
    src/fetch.cpp
    src/fetch.hpp
    src/installer.cpp
    src/installer.hpp
    src/manager.cpp
    src/manager.hpp
    src/manager-dock.cpp
    src/manager-dock.hpp
    src/plugin.hpp
    src/properties-form.cpp
    src/properties-form.hpp
    src/self-test.cpp
    src/store.cpp
    src/store.hpp
    src/twitch-api.cpp
    src/twitch-api.hpp
    src/twitch-chat.cpp
    src/twitch-chat.hpp
    src/twitch-http.cpp
    src/twitch-http.hpp
    src/vtube-studio.cpp
    src/vtube-studio.hpp
    src/websocket.cpp
    src/websocket.hpp
    src/zip.cpp
    src/zip.hpp
  MODULE_SOURCES src/plugin-main.cpp
  # libcurl and zlib come with OBS, beside obs.dll; version reads a DLL's version
  LIBRARIES CURL::libcurl ZLIB::ZLIB ws2_32 version Qt6::Network
)

foreach(_target IN ITEMS sarxina-obs-plugin-manager sarxina-obs-plugin-manager-selftest)
  target_compile_definitions(${_target} PRIVATE SARXINA_TWITCH_CLIENT_ID="${SARXINA_TWITCH_CLIENT_ID}")
endforeach()
