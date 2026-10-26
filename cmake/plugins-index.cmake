# cmake -DINPUT=<list> -DDIST=<dist folder> -DOUTPUT=<plugins.json> -P plugins-index.cmake
#
# Writes plugins.json, the list the Sarxina OBS Plugin Manager installs plugins from: the list made when the build was
# configured (INPUT, see sarxina_add_plugins_index in ObsPlugin.cmake), with the SHA-256 of each zip in DIST, the
# plugins' and the manager's.

file(READ "${INPUT}" _index)

# Puts the SHA-256 of the zip an entry names in place of its name; the entry is at the JSON path given.
macro(_sha256_for)
  string(JSON _zip GET "${_index}" ${ARGN} zip)
  if(NOT EXISTS "${DIST}/${_zip}")
    message(FATAL_ERROR "${DIST}/${_zip} is missing")
  endif()
  file(SHA256 "${DIST}/${_zip}" _sha256)
  string(JSON _index REMOVE "${_index}" ${ARGN} zip)
  string(JSON _index SET "${_index}" ${ARGN} sha256 "\"${_sha256}\"")
endmacro()

string(JSON _count LENGTH "${_index}" plugins)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE ${_last})
  _sha256_for(plugins ${_i})
endforeach()
_sha256_for(manager)
file(WRITE "${OUTPUT}" "${_index}\n")
message(STATUS "Wrote ${OUTPUT} with ${_count} plugins")
