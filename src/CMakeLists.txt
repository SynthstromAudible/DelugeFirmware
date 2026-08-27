cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(foundation) # bottom of the stack; the layers below link it
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(gfx)        # deluge_gfx_oled / deluge_gfx_grid: pure drawing primitives
add_subdirectory(ui)         # deluge_ui_core / deluge_ui_oled / deluge_ui_grid: the immediate-mode toolkit
add_subdirectory(notify)     # deluge_notify: model->UI notification channel
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
add_subdirectory(NE10)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/bsp/rust: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/bsp/rust/build.rs). The host-sim assembles the same deluge_app against
# its own BSP in sim/CMakeLists.txt.
# ---------------------------------------------------------------------------
