# cmake -DGAME=<chatvillage/game> -DOUTPUT=<folder> -P deploy-game.cmake
#
# Puts the game as players run it into OUTPUT: the engine, the data and the plugins, and of RPG Maker's and Time
# Fantasy's pictures and sounds only those the town uses, the way RPG Maker MZ's own deployment leaves unused files
# out. It needs the engine and the art in GAME (get-engine-files.ps1).

if(NOT EXISTS "${GAME}/js/rmmz_core.js" OR NOT EXISTS "${GAME}/img/tilesets")
  message(FATAL_ERROR "The game has no engine or art yet: run chatvillage/game/get-engine-files.ps1 first.")
endif()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

# Copies files of the game into the same place in OUTPUT; a missing one stops the build.
function(take)
  foreach(_path IN LISTS ARGN)
    if(NOT EXISTS "${GAME}/${_path}")
      message(FATAL_ERROR "The game uses ${_path}, which is not there.")
    endif()
    get_filename_component(_folder "${OUTPUT}/${_path}" DIRECTORY)
    file(COPY "${GAME}/${_path}" DESTINATION "${_folder}")
  endforeach()
endfunction()

# Reads one of the game's data files.
function(read_data name out)
  file(READ "${GAME}/data/${name}" _json)
  set(${out} "${_json}" PARENT_SCOPE)
endfunction()

# The value at a path in JSON, or "" when it is not there.
function(json_get out json)
  string(JSON _value ERROR_VARIABLE _missing GET "${json}" ${ARGN})
  if(_missing)
    set(_value "")
  endif()
  set(${out} "${_value}" PARENT_SCOPE)
endfunction()

# The engine, the page it runs in, the data and the plugins.
take(index.html css fonts icon js data img/system img/characters)

# The faces of the actors, for the menu.
read_data(Actors.json _actors)
string(JSON _count LENGTH "${_actors}")
math(EXPR _last "${_count} - 1")
foreach(_at RANGE 1 ${_last})
  json_get(_face "${_actors}" ${_at} faceName)
  if(_face)
    take(img/faces/${_face}.png)
  endif()
endforeach()

# The tilesets of the maps, and any music they play.
read_data(Tilesets.json _tilesets)
file(GLOB _maps RELATIVE "${GAME}/data" "${GAME}/data/Map[0-9][0-9][0-9].json")
foreach(_map IN LISTS _maps)
  read_data(${_map} _json)
  json_get(_tileset "${_json}" tilesetId)
  foreach(_slot RANGE 0 8)
    json_get(_name "${_tilesets}" ${_tileset} tilesetNames ${_slot})
    if(_name)
      take(img/tilesets/${_name}.png)
    endif()
  endforeach()
  foreach(_kind IN ITEMS bgm bgs)
    json_get(_name "${_json}" ${_kind} name)
    if(_name)
      take(audio/${_kind}/${_name}.ogg)
    endif()
  endforeach()
endforeach()

# The menus' sounds.
read_data(System.json _system)
string(JSON _count LENGTH "${_system}" sounds)
math(EXPR _last "${_count} - 1")
foreach(_at RANGE 0 ${_last})
  json_get(_name "${_system}" sounds ${_at} name)
  if(_name)
    take(audio/se/${_name}.ogg)
  endif()
endforeach()
