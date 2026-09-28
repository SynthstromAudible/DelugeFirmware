# The integration scenarios: stepped scenarios the Embassy host harness runs (scenario_table.cpp), and the probes and
# oracles the host executables call. Host-only, so only the host tree adds this directory (sim/CMakeLists.txt): the
# firmware image never contains them.
#
# An OBJECT library, like deluge_app, so its objects sit in one directory the host harnesses' build scripts archive
# alongside the app's (golden_vt_render, lens1_vt_sim, deluge-bsp-rust's host_app build).
file(GLOB deluge_scenarios_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
add_library(deluge_scenarios OBJECT ${deluge_scenarios_SOURCES})

# The app's include paths, definitions and options: scenarios drive the app's own code.
target_link_libraries(deluge_scenarios PRIVATE deluge_app)
# <scenarios/...> for the scenarios themselves and for the host executables that call them.
target_include_directories(deluge_scenarios PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
