# The smallest plugin the Sarxina OBS Plugin Manager can run (see BUILDING.md). Built with the rest so that it keeps
# working, but never packaged or released.
add_library(example-plugin MODULE example-plugin.cpp)
target_include_directories(example-plugin PRIVATE "${SARXINA_SHARED_DIR}/src") # for sarxina-obs-plugin-manager.h
target_link_libraries(example-plugin PRIVATE OBS::libobs)
target_compile_options(example-plugin PRIVATE /W4 /utf-8 /permissive-)
target_link_options(example-plugin PRIVATE "/PDBALTPATH:%_PDB%")
set_target_properties(example-plugin PROPERTIES PREFIX "" CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
