# deluge_synth: the Deluge synth library — the per-voice sources and the modulation core, and (with the voice lift)
# the L1 engines and the L2 voice kernel. Built with the library flags, below the app.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deluge_library.cmake)

file(GLOB_RECURSE _synth_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
add_library(deluge_synth STATIC ${_synth_srcs})
deluge_library_target(deluge_synth)
target_include_directories(deluge_synth PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
# src/deluge stays on the path while the modulation core reads modulation/ (the mod matrix's first milestone cuts it),
# the input source reaches the allocator seam and the vocabulary comes through definitions_cxx.hpp. The synth boundary
# check limits what it may reach.
target_include_directories(deluge_synth PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../deluge)
target_link_libraries(deluge_synth PUBLIC deluge_dsp_next deluge_plugin_types deluge_foundation PRIVATE argon etl::etl)
