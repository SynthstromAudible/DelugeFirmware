cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(foundation) # bottom of the stack; the layers below link it
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(gfx)        # deluge_gfx_oled / deluge_gfx_grid: pure drawing primitives
add_subdirectory(ui)         # deluge_ui_core / deluge_ui_oled / deluge_ui_grid: the immediate-mode toolkit
add_subdirectory(shell)      # deluge_ui_shell: surfaces, scene stack, input lowering, services, tick, flush
add_subdirectory(notify)     # deluge_notify: model->UI notification channel
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
# deluge_scenes: the first UI layer allowed to include the model (ruling Q4). Links deluge_app;
# must come after deluge_app exists to link against it. Linked into the firmware and the host sim
# by src/bsp/rust/build.rs (both the device and host archive lists), and into its own spec
# executable by sim/scenes_spec/CMakeLists.txt.
add_subdirectory(scenes)
add_subdirectory(NE10)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/bsp/rust: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/bsp/rust/build.rs). The host-sim assembles the same deluge_app against
# its own BSP in sim/CMakeLists.txt.
# ---------------------------------------------------------------------------
