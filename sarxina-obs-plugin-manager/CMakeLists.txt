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

set(_manager_version 1.4.0)

sarxina_add_obs_module(
  sarxina-obs-plugin-manager
  TITLE "Sarxina OBS Plugin Manager"
  VERSION ${_manager_version}
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
    src/self-update.cpp
    src/self-update.hpp
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
  MODULE_SOURCES src/plugin-main.cpp src/source-menu.cpp src/source-menu.hpp
  # libcurl and zlib come with OBS, beside obs.dll; version reads a DLL's version
  LIBRARIES CURL::libcurl ZLIB::ZLIB ws2_32 version Qt6::Network
)

foreach(_target IN ITEMS sarxina-obs-plugin-manager sarxina-obs-plugin-manager-selftest)
  target_compile_definitions(${_target} PRIVATE SARXINA_TWITCH_CLIENT_ID="${SARXINA_TWITCH_CLIENT_ID}")
endforeach()

# The installer, dist/Sarxina-OBS-Plugin-Manager-Setup.exe, made with NSIS 3 from this version's zip in dist/ (the
# released one, so the installer holds exactly its files). Its name carries no version, so the README's link to
# releases/latest/download/ always finds it.
find_program(SARXINA_MAKENSIS makensis PATHS "$ENV{ProgramFiles\(x86\)}/NSIS" "$ENV{ProgramFiles}/NSIS")
add_custom_target(
  sarxina-obs-plugin-manager-installer
  COMMAND "${CMAKE_COMMAND}"
          "-DZIP=${CMAKE_SOURCE_DIR}/dist/sarxina-obs-plugin-manager-${_manager_version}-windows-x64.zip"
          "-DVERSION=${_manager_version}"
          "-DSTAGE=${CMAKE_BINARY_DIR}/installer/sarxina-obs-plugin-manager"
          "-DNSI=${CMAKE_CURRENT_SOURCE_DIR}/installer/installer.nsi"
          "-DMAKENSIS=${SARXINA_MAKENSIS}"
          "-DOUTFILE=${CMAKE_SOURCE_DIR}/dist/Sarxina-OBS-Plugin-Manager-Setup.exe"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/installer/installer.cmake"
  COMMENT "Building the manager's installer into dist/"
  VERBATIM
)
