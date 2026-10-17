# cmake -DINPUT=<list> -DDIST=<dist folder> -DOUTPUT=<plugins.json> -P plugins-index.cmake
#
# Writes plugins.json, the list the Sarxina OBS Plugin Manager installs plugins from: the list made when the build was
# configured (INPUT, see sarxina_add_plugins_index in ObsPlugin.cmake), with the SHA-256 of each plugin's zip in DIST.

file(READ "${INPUT}" _index)
string(JSON _count LENGTH "${_index}" plugins)
math(EXPR _last "${_count} - 1")
foreach(_i RANGE ${_last})
  string(JSON _zip GET "${_index}" plugins ${_i} zip)
  if(NOT EXISTS "${DIST}/${_zip}")
    message(FATAL_ERROR "${DIST}/${_zip} is missing")
  endif()
  file(SHA256 "${DIST}/${_zip}" _sha256)
  string(JSON _index REMOVE "${_index}" plugins ${_i} zip)
  string(JSON _index SET "${_index}" plugins ${_i} sha256 "\"${_sha256}\"")
endforeach()
file(WRITE "${OUTPUT}" "${_index}\n")
message(STATUS "Wrote ${OUTPUT} with ${_count} plugins")
