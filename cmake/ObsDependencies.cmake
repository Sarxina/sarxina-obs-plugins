# Fetches what an OBS Studio plugin needs to build, the same way the official plugin template does:
#   1. the OBS Studio sources, the pre-built obs-deps bundle and the pre-built Qt 6 bundle listed in buildspec.json
#      are downloaded into .deps/ and checked against their SHA-256,
#   2. libobs and obs-frontend-api are built from the OBS sources and installed into .deps/obs-sdk-<version>-x64,
#   3. CMAKE_PREFIX_PATH is pointed at the results so find_package(libobs), find_package(obs-frontend-api) and
#      find_package(Qt6) work.
# Everything is skipped on later runs unless buildspec.json changes.

include_guard(GLOBAL)

if(NOT CMAKE_GENERATOR MATCHES "Visual Studio" OR NOT CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
  message(FATAL_ERROR "These plugins are built for Windows x64 with a Visual Studio generator. Use the windows-x64 preset.")
endif()

set(SARXINA_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Folder the build dependencies are downloaded into")
file(MAKE_DIRECTORY "${SARXINA_DEPS_DIR}")
file(READ "${CMAKE_SOURCE_DIR}/buildspec.json" _sarxina_buildspec)

# _sarxina_fetch(<key> <out-directory-var> <out-version-var>): download, verify and unpack one entry of buildspec.json
function(_sarxina_fetch key out_directory out_version)
  foreach(_field IN ITEMS label version url sha256 file directory archiveHasTopLevelDirectory)
    string(JSON _${_field} GET "${_sarxina_buildspec}" dependencies ${key} ${_field})
  endforeach()

  set(_archive "${SARXINA_DEPS_DIR}/${_file}")
  set(_destination "${SARXINA_DEPS_DIR}/${_directory}")
  set(_stamp "${SARXINA_DEPS_DIR}/.${key}-${_sha256}.stamp")

  if(NOT EXISTS "${_stamp}" OR NOT IS_DIRECTORY "${_destination}")
    set(_have "")
    if(EXISTS "${_archive}")
      file(SHA256 "${_archive}" _have)
    endif()

    if(NOT _have STREQUAL _sha256)
      message(STATUS "Downloading ${_label} ${_version}")
      file(DOWNLOAD "${_url}" "${_archive}" EXPECTED_HASH SHA256=${_sha256} STATUS _status)
      list(GET _status 0 _code)
      if(NOT _code EQUAL 0)
        list(GET _status 1 _why)
        file(REMOVE "${_archive}")
        message(FATAL_ERROR "Could not download ${_url}: ${_why}")
      endif()
    endif()

    message(STATUS "Unpacking ${_label} ${_version}")
    file(REMOVE_RECURSE "${_destination}")
    if(_archiveHasTopLevelDirectory)
      file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${SARXINA_DEPS_DIR}")
    else()
      file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_destination}")
    endif()
    file(WRITE "${_stamp}" "${_version}\n")
  endif()

  set(${out_directory} "${_destination}" PARENT_SCOPE)
  set(${out_version} "${_version}" PARENT_SCOPE)
endfunction()

# _sarxina_run(<what> <log-name> <command...>): run one step of the OBS build, keeping its output in .deps/
function(_sarxina_run what log)
  message(STATUS "${what}")
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _result
    OUTPUT_FILE "${SARXINA_DEPS_DIR}/${log}.log"
    ERROR_FILE "${SARXINA_DEPS_DIR}/${log}.log"
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "${what} failed (${_result}). See ${SARXINA_DEPS_DIR}/${log}.log")
  endif()
endfunction()

_sarxina_fetch(prebuilt _obs_deps_dir _obs_deps_version)
_sarxina_fetch(qt6 _qt6_dir _qt6_version)
_sarxina_fetch(obs-studio _obs_source_dir OBS_STUDIO_VERSION)

set(_obs_build_dir "${_obs_source_dir}/build_x64")
set(_obs_sdk_dir "${SARXINA_DEPS_DIR}/obs-sdk-${OBS_STUDIO_VERSION}-x64")
set(_obs_sdk_stamp "${_obs_sdk_dir}/.built-with-deps-${_obs_deps_version}.stamp")

if(NOT EXISTS "${_obs_sdk_stamp}")
  _sarxina_run(
    "Configuring OBS Studio ${OBS_STUDIO_VERSION} (libobs and obs-frontend-api only)"
    obs-studio-configure
    "${CMAKE_COMMAND}"
    -S "${_obs_source_dir}"
    -B "${_obs_build_dir}"
    -G "${CMAKE_GENERATOR}"
    -A "x64,version=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    -DOBS_CMAKE_VERSION:STRING=3.0.0
    -DENABLE_PLUGINS:BOOL=OFF
    -DENABLE_FRONTEND:BOOL=OFF
    -DENABLE_SCRIPTING:BOOL=OFF
    -DOBS_VERSION_OVERRIDE:STRING=${OBS_STUDIO_VERSION}
    "-DCMAKE_PREFIX_PATH:PATH=${_obs_deps_dir};${_qt6_dir}"
  )
  _sarxina_run(
    "Building libobs and obs-frontend-api ${OBS_STUDIO_VERSION} (this takes a few minutes the first time)"
    obs-studio-build
    "${CMAKE_COMMAND}" --build "${_obs_build_dir}" --target obs-frontend-api --config RelWithDebInfo --parallel
  )
  file(REMOVE_RECURSE "${_obs_sdk_dir}")
  _sarxina_run(
    "Installing the OBS Studio ${OBS_STUDIO_VERSION} development files"
    obs-studio-install
    "${CMAKE_COMMAND}" --install "${_obs_build_dir}" --component Development --config RelWithDebInfo --prefix
    "${_obs_sdk_dir}"
  )
  file(WRITE "${_obs_sdk_stamp}" "${OBS_STUDIO_VERSION}\n")
endif()

set(SARXINA_OBS_SDK_DIR "${_obs_sdk_dir}")
set(SARXINA_QT6_DIR "${_qt6_dir}")
set(SARXINA_OBS_DEPS_DIR "${_obs_deps_dir}")

list(PREPEND CMAKE_PREFIX_PATH "${SARXINA_OBS_SDK_DIR}" "${SARXINA_QT6_DIR}" "${SARXINA_OBS_DEPS_DIR}")
list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

message(STATUS "OBS Studio ${OBS_STUDIO_VERSION}, Qt ${Qt6_VERSION}")
