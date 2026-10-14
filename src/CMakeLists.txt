cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(engine-api/types) # deluge_engine_types: the host–engine contract's data-only types
add_subdirectory(foundation) # bottom of the stack; the layers below link it
add_subdirectory(serialization) # deluge_serialization: the document tree and its XML and JSON parsers
add_subdirectory(engine-api) # deluge_engine_api: the host–engine contract's interface
add_subdirectory(engines/effects) # deluge_effects: the effects' words, state and codec
add_subdirectory(engines/deluge) # deluge_instrument: the Deluge instrument's state, words and codec
add_subdirectory(engines/global_effectable) # deluge_global_effectable: the provisional shared-effect param table
add_subdirectory(music-theory) # deluge_music_theory: note sets, scales, keys and scale mapping
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(deluge-project) # deluge_project: the project model (deluge::project::Song)
add_subdirectory(gfx)        # deluge_gfx_oled / deluge_gfx_grid: pure drawing primitives
add_subdirectory(ui)         # deluge_ui_core / deluge_ui_oled / deluge_ui_grid: the immediate-mode toolkit
add_subdirectory(shell)      # deluge_ui_shell: surfaces, scene stack, input lowering, services, tick, flush
add_subdirectory(notify)     # deluge_notify: model->UI notification channel
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
# deluge_scenes: the first UI layer allowed to include the model. Links deluge_app, so it comes
# after deluge_app exists. Archived into the firmware and the host app by the Rust BSP's build
# scripts beside deluge_app.
add_subdirectory(scenes)
add_subdirectory(NE10)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/bsp/rust: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/bsp/rust/build/). sim/CMakeLists.txt builds the same libraries for the
# host, which the Rust BSP's host builds link.
# ---------------------------------------------------------------------------
