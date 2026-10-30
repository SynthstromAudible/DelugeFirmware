cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(plugin-api/types) # deluge_plugin_types: the host–plugin contract's data-only types
add_subdirectory(foundation) # bottom of the stack; the layers below link it
add_subdirectory(serialization) # deluge_serialization: the document tree and its XML and JSON parsers
add_subdirectory(plugin-api) # deluge_plugin_api: the host–plugin contract's interface
add_subdirectory(engines/effects) # deluge_effects: the effects' words, state and codec
add_subdirectory(engines/deluge) # deluge_instrument: the Deluge instrument's state, words and codec
add_subdirectory(engines/global_effectable) # deluge_global_effectable: the provisional shared-effect param table
add_subdirectory(music-theory) # deluge_music_theory: note sets, scales, keys and scale mapping
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(deluge-project) # deluge_project: the project model (deluge::project::Song)
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/bsp/rust: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/bsp/rust/build/). sim/CMakeLists.txt builds the same libraries for the
# host, which the Rust BSP's host builds link.
# ---------------------------------------------------------------------------
