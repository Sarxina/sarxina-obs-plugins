# Builds the manager's installer from its released zip, so the installer holds exactly the files of that release.
# Run by the target sarxina-obs-plugin-manager-installer with:
#   ZIP       dist/sarxina-obs-plugin-manager-<version>-windows-x64.zip
#   VERSION   the manager's version
#   STAGE     a scratch folder
#   NSI       installer.nsi
#   MAKENSIS  makensis.exe of NSIS 3
#   OUTFILE   the installer to write

if(NOT EXISTS "${ZIP}")
  message(FATAL_ERROR "${ZIP} is missing. Download it from the manager's release (gh release download "
                      "sarxina-obs-plugin-manager-v${VERSION} -D dist), or build sarxina-obs-plugin-manager-package "
                      "for a manager that is not released yet.")
endif()

file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}/zip")
file(ARCHIVE_EXTRACT INPUT "${ZIP}" DESTINATION "${STAGE}/zip")

# The zip is laid out like the OBS folder; OBS 32.2 loads a plugin in ProgramData from <plugin>\bin\64bit and
# <plugin>\data
set(_dll "${STAGE}/zip/obs-plugins/64bit/sarxina-obs-plugin-manager.dll")
set(_data "${STAGE}/zip/data/obs-plugins/sarxina-obs-plugin-manager")
if(NOT EXISTS "${_dll}" OR NOT IS_DIRECTORY "${_data}")
  message(FATAL_ERROR "${ZIP} is not laid out as the manager's zip")
endif()
file(MAKE_DIRECTORY "${STAGE}/plugin/bin/64bit")
file(COPY "${_dll}" DESTINATION "${STAGE}/plugin/bin/64bit")
file(COPY "${_data}/" DESTINATION "${STAGE}/plugin/data")

file(TO_NATIVE_PATH "${STAGE}/plugin" _stage_native)
file(TO_NATIVE_PATH "${OUTFILE}" _out_native)
execute_process(
  COMMAND "${MAKENSIS}" /V2 /WX "/DVERSION=${VERSION}" "/DSTAGE=${_stage_native}" "/DOUTFILE=${_out_native}" "${NSI}"
  RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "makensis failed (${_result})")
endif()
file(SHA256 "${OUTFILE}" _sha)
message(STATUS "Installer: ${OUTFILE} (SHA-256 ${_sha})")
