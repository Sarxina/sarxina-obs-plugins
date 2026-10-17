find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED) # zlib.dll comes with OBS too, to unpack the plugins' zips
find_package(Qt6 REQUIRED COMPONENTS Network) # for the self-test's stand-in servers; OBS ships Qt6Network.dll

sarxina_add_obs_module(
  sarxina-obs-plugin-manager
  TITLE "Sarxina OBS Plugin Manager"
  VERSION 1.0.0
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
    src/twitch-chat.cpp
    src/twitch-chat.hpp
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
