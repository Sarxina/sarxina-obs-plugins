# sarxina_add_obs_plugin(<name> VERSION <x.y.z> DESCRIPTION <text> SOURCES <files...> [DATA <directory>])
#
# Declares one OBS Studio plugin. Each plugin lives in its own folder beside the others and gets:
#   <name>            the plugin itself (<name>.dll)
#   <name>-package    dist/<name>-<version>-windows-x64.zip, laid out so that extracting it into an OBS Studio
#                     folder installs the plugin:
#                       obs-plugins/64bit/<name>.dll
#                       data/obs-plugins/<name>/...
#
# sarxina_add_plugin_check(<name> SOURCES <files...> [ARGUMENTS <arguments...>])
#
# Declares a console program that checks a plugin's code without OBS, and registers it with CTest.

include_guard(GLOBAL)

# Settings shared by everything built against OBS and Qt
function(_sarxina_target_settings target)
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
  cmake_parse_arguments(PARSE_ARGV 1 _plugin "" "VERSION;DESCRIPTION;DATA" "SOURCES")
  if(NOT _plugin_VERSION OR NOT _plugin_SOURCES)
    message(FATAL_ERROR "sarxina_add_obs_plugin(${name}) needs VERSION and SOURCES")
  endif()

  # Version details shown by Windows in the file's properties
  string(REPLACE "." "," PLUGIN_VERSION_COMMAS "${_plugin_VERSION}")
  set(PLUGIN_NAME "${name}")
  set(PLUGIN_VERSION "${_plugin_VERSION}")
  set(PLUGIN_DESCRIPTION "${_plugin_DESCRIPTION}")
  configure_file("${CMAKE_SOURCE_DIR}/cmake/plugin-version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${name}-version.rc" @ONLY)

  add_library(${name} MODULE ${_plugin_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/${name}-version.rc")
  _sarxina_target_settings(${name})
  target_compile_definitions(${name} PRIVATE PLUGIN_NAME="${name}" PLUGIN_VERSION="${_plugin_VERSION}")
  set_target_properties(${name} PROPERTIES PREFIX "")

  install(TARGETS ${name} LIBRARY DESTINATION obs-plugins/64bit COMPONENT ${name})
  if(_plugin_DATA)
    install(DIRECTORY "${_plugin_DATA}/" DESTINATION data/obs-plugins/${name} COMPONENT ${name})
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION data/obs-plugins/${name} RENAME LICENSE.txt COMPONENT ${name})
  endif()

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

function(sarxina_add_plugin_check name)
  cmake_parse_arguments(PARSE_ARGV 1 _check "" "" "SOURCES;ARGUMENTS")

  add_executable(${name} ${_check_SOURCES})
  _sarxina_target_settings(${name})

  # The program needs the OBS and Qt libraries beside it; point it at the ones in .deps instead of copying them.
  add_test(NAME ${name} COMMAND ${name} ${_check_ARGUMENTS})
  set_tests_properties(
    ${name}
    PROPERTIES
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${SARXINA_OBS_DEPS_DIR}/bin;PATH=path_list_prepend:${SARXINA_QT6_DIR}/bin;PATH=path_list_prepend:${SARXINA_OBS_SDK_DIR}/bin/64bit"
  )
endfunction()
