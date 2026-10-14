# sarxina_add_obs_plugin(<name> TITLE <title> VERSION <x.y.z> DESCRIPTION <text> SOURCES <files...>)
#
# Declares one OBS Studio plugin: the shared dock (shared/) with the plugin's own game in it. The plugin's sources
# define makeGame() and runSelfTest(), and its folder has data/locale/*.ini with its own strings; the shared strings
# in shared/data/locale are added to those. Each plugin gets:
#   <name>            the plugin itself (<name>.dll)
#   <name>-selftest   its self-test as a program that needs no OBS, registered with CTest
#   <name>-package    dist/<name>-<version>-windows-x64.zip, laid out so that extracting it into an OBS Studio
#                     folder installs the plugin:
#                       obs-plugins/64bit/<name>.dll
#                       data/obs-plugins/<name>/...

include_guard(GLOBAL)

set(SARXINA_SHARED_DIR "${CMAKE_CURRENT_LIST_DIR}/../shared")
cmake_path(NORMAL_PATH SARXINA_SHARED_DIR)

# Settings shared by everything built against OBS and Qt
function(sarxina_target_settings target)
  target_link_libraries(${target} PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /MP /Zc:__cplusplus)
  target_link_options(${target} PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>")
  # Keep the build folder's full path out of the released binary: store only the .pdb file name
  target_link_options(${target} PRIVATE "/PDBALTPATH:%_PDB%")
  set_target_properties(
    ${target}
    PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endfunction()

function(sarxina_add_obs_plugin name)
  cmake_parse_arguments(PARSE_ARGV 1 _plugin "" "TITLE;VERSION;DESCRIPTION" "SOURCES")
  if(NOT _plugin_TITLE OR NOT _plugin_VERSION OR NOT _plugin_SOURCES)
    message(FATAL_ERROR "sarxina_add_obs_plugin(${name}) needs TITLE, VERSION and SOURCES")
  endif()

  # The plugin's strings with the shared ones added
  set(_data "${CMAKE_CURRENT_BINARY_DIR}/data")
  file(GLOB _locales RELATIVE "${SARXINA_SHARED_DIR}/data/locale" "${SARXINA_SHARED_DIR}/data/locale/*.ini")
  foreach(_locale IN LISTS _locales)
    set(_own "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/${_locale}")
    set(_shared "${SARXINA_SHARED_DIR}/data/locale/${_locale}")
    file(READ "${_own}" _own_text)
    file(READ "${_shared}" _shared_text)
    file(CONFIGURE OUTPUT "${_data}/locale/${_locale}" CONTENT "${_own_text}\n${_shared_text}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_own}" "${_shared}")
  endforeach()

  # Version details shown by Windows in the file's properties
  string(REPLACE "." "," PLUGIN_VERSION_COMMAS "${_plugin_VERSION}")
  set(PLUGIN_NAME "${name}")
  set(PLUGIN_VERSION "${_plugin_VERSION}")
  set(PLUGIN_DESCRIPTION "${_plugin_DESCRIPTION}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/plugin-version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${name}-version.rc" @ONLY)

  set(_definitions PLUGIN_NAME="${name}" PLUGIN_TITLE="${_plugin_TITLE}" PLUGIN_VERSION="${_plugin_VERSION}")

  add_library(
    ${name} MODULE
    ${_plugin_SOURCES} "${SARXINA_SHARED_DIR}/src/plugin-main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/${name}-version.rc"
  )
  sarxina_target_settings(${name})
  target_link_libraries(${name} PRIVATE sarxina-shared)
  target_compile_definitions(${name} PRIVATE ${_definitions})
  set_target_properties(${name} PROPERTIES PREFIX "")

  # The self-test the plugin can run inside OBS, as a program that needs no OBS and shows nothing on screen:
  #   ctest --test-dir build_x64 -C RelWithDebInfo -V
  add_executable(${name}-selftest ${_plugin_SOURCES} "${SARXINA_SHARED_DIR}/tests/selftest-main.cpp")
  sarxina_target_settings(${name}-selftest)
  target_link_libraries(${name}-selftest PRIVATE sarxina-shared)
  target_compile_definitions(${name}-selftest PRIVATE ${_definitions})
  add_test(NAME ${name}-selftest COMMAND ${name}-selftest "${_data}/locale/en-US.ini" "${CMAKE_BINARY_DIR}/${name}-pictures")
  # The program needs the OBS and Qt libraries beside it; point it at the ones in .deps instead of copying them.
  set_tests_properties(
    ${name}-selftest
    PROPERTIES
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${SARXINA_OBS_DEPS_DIR}/bin;PATH=path_list_prepend:${SARXINA_QT6_DIR}/bin;PATH=path_list_prepend:${SARXINA_OBS_SDK_DIR}/bin/64bit"
  )

  install(TARGETS ${name} LIBRARY DESTINATION obs-plugins/64bit COMPONENT ${name})
  install(DIRECTORY "${_data}/" DESTINATION data/obs-plugins/${name} COMPONENT ${name})
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION data/obs-plugins/${name} RENAME LICENSE.txt COMPONENT ${name})

  set(_stage "${CMAKE_BINARY_DIR}/package/${name}")
  set(_zip "${CMAKE_SOURCE_DIR}/dist/${name}-${_plugin_VERSION}-windows-x64.zip")
  add_custom_target(
    ${name}-package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_stage}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG> --component ${name} --prefix "${_stage}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/dist"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${_zip}"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${_stage}" "${CMAKE_COMMAND}" -E tar cf "${_zip}" --format=zip -- obs-plugins data
    DEPENDS ${name}
    COMMENT "Packaging ${name} ${_plugin_VERSION} into dist/"
    VERBATIM
  )
endfunction()
