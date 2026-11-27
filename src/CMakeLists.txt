cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})
# Vendored headers (<gsl/gsl>) come from third_party/, as system includes.
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/third_party)

add_subdirectory(plugin/types) # deluge_plugin_types: the host–plugin contract's data-only types
add_subdirectory(base) # bottom of the stack; the layers below link it
add_subdirectory(dsp) # deluge_dsp: the L0 DSP library (the core interfaces and the fixed and float backends)
add_subdirectory(serialization) # deluge_serialization: the document tree and its XML and JSON parsers
add_subdirectory(plugin) # deluge_plugin: the host–plugin contract's interface
add_subdirectory(effects) # deluge_effects: the effects' words, state and codec
add_subdirectory(arp) # deluge_arp: the arpeggiator's param table and the values an arp plays by
add_subdirectory(instruments/classic) # deluge_instruments_classic: the Deluge instrument's state, words and codec
add_subdirectory(host/chains) # deluge_host_chains: the host chain's param table and pages
add_subdirectory(music) # deluge_music: note sets, scales, keys and scale mapping
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(project) # deluge_project: the project model (deluge::project::Song)
add_subdirectory(synth)      # deluge_synth: the synth library (sources, modulation core)
add_subdirectory(ui)         # deluge_ui_*: gfx, widgets, controls, shell, the waveform view and the scenes (src/ui/CMakeLists.txt)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/printf ${CMAKE_BINARY_DIR}/printf)

# ---------------------------------------------------------------------------
# No firmware executable is assembled here. The device image is linked by cargo
# from src/platform/device: it archives deluge_app's objects plus the static-library
# closure above and links them against the Rust BSP (see scripts/tasks/task-build.py
# and src/platform/device/build/). src/platform/host/CMakeLists.txt builds the same libraries for the
# host, which the Rust BSP's host builds link.
# ---------------------------------------------------------------------------
