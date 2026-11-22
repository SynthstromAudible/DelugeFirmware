cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(plugin/types) # deluge_plugin_types: the host–plugin contract's data-only types
add_subdirectory(base) # bottom of the stack; the layers below link it
add_subdirectory(serialization) # deluge_serialization: the document tree and its XML and JSON parsers
add_subdirectory(plugin) # deluge_plugin: the host–plugin contract's interface
add_subdirectory(effects) # deluge_effects: the effects' words, state and codec
add_subdirectory(instruments/classic) # deluge_instruments_classic: the Deluge instrument's state, words and codec
add_subdirectory(host/chains) # deluge_host_chains: the host chain's param table and pages
add_subdirectory(music) # deluge_music: note sets, scales, keys and scale mapping
add_subdirectory(sample-view) # deluge_sample_view: a waveform view's columns, peaks and navigation
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(project) # deluge_project: the project model (deluge::project::Song)
add_subdirectory(controls)   # deluge_controls: the gold knobs' focus and the controls vocabulary
add_subdirectory(synth)      # deluge_synth: the synth library (sources, modulation core)
add_subdirectory(ui)         # deluge_ui_widgets_core / _oled / _grid: the immediate-mode toolkit
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
# deluge_scenes (with deluge_scenes_bind): the first UI layer allowed to include the model. It
# compiles against deluge_app, so it comes after deluge_app exists. Archived into the firmware by
# the Rust BSP's build script beside deluge_app.
add_subdirectory(scenes)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/bsp/rust: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/bsp/rust/build/). sim/CMakeLists.txt builds the same libraries for the
# host, which the Rust BSP's host builds link.
# ---------------------------------------------------------------------------
