# The scenes' specs, linked into the host app and run one file per process by golden_vt_render's scenes_spec
# scenario (tests/integration/scenarios/scenes_spec_scenario.cpp). The entry table is generated from the files present:
# a spec file's id is its path here without the extension, and its entry the id with '/' as '_'.
FetchContent_MakeAvailable(CppSpec)

file(GLOB_RECURSE _scenes_specs CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
# The files ported to next so far. Each is built and run; the rest wait for their batch.
set(SCENES_SPEC_PORTED
    selftest/pass_spec
    selftest/fail_spec
    selftest/crash_spec
    selftest/card_access_spec
    menu_scene_spec
    midi_learn_spec
    mod_knobs_spec
    menu_binding_hooks_spec
    bindings_spec
    context_spec
    menu_grid_claim_spec
    menu_actions_spec
    menu_entry_spec
    menu_ranges_spec
    menu_scales_spec
    menu_horizontal_controls_spec
    shortcuts_spec
    tables_1_spec
    tables_2_spec
    output_items_spec
    sample_items_spec
    param_popup_spec
    note_mode_spec
    note_mode_reads_spec
    note_nodes_spec
    note_roots_spec
    note_bridge_spec
    pad_forward_spec
    global_controls_spec
    count_in_spec
    timeline_spec
    midi_device_spec
    audio_clip_scene_spec
    marker_scene_spec
    rename_scene_spec
    screensaver_scene_spec
    scene_scripts_spec
    boot_wiring_spec
    notification_sink_spec
    profile_spec
    l10n_bridge_spec
    audio_source_spec
)
set(_ported_sources "")
set(_decls "")
set(_rows "")
foreach(_spec IN LISTS _scenes_specs)
  cmake_path(REMOVE_EXTENSION _spec OUTPUT_VARIABLE _id)
  if(NOT _id IN_LIST SCENES_SPEC_PORTED)
    continue()
  endif()
  string(REPLACE "/" "_" _fn "${_id}")
  list(APPEND _ported_sources ${_spec})
  string(APPEND _decls "int ${_fn}(int, char**);\n")
  string(APPEND _rows "    {\"${_id}\", &${_fn}},\n")
endforeach()
configure_file(${CMAKE_CURRENT_LIST_DIR}/entries.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/entries.cpp @ONLY)

# The examples' shared support: the screensaver oracles and the harness's state-line writer.
set(_support_sources
    support/oracle_rainfall.cpp
    support/oracle_starfield.cpp
    ${REPO_ROOT}/tests/harness/snapshot.cpp)

add_library(deluge_scenes_spec STATIC ${_ported_sources} ${_support_sources} ${CMAKE_CURRENT_BINARY_DIR}/entries.cpp)
target_link_libraries(deluge_scenes_spec PRIVATE deluge_app deluge_scenes c++spec)
target_include_directories(deluge_scenes_spec PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(deluge_scenes_spec PRIVATE ${REPO_ROOT}/tests ${REPO_ROOT}/tests/unit)
target_compile_definitions(deluge_scenes_spec PRIVATE
    GFX_GOLDEN_DIR="${REPO_ROOT}/tests/golden/gfx"
    DELUGE_REPO_ROOT="${REPO_ROOT}"
    UI_SCRIPTS_DIR="${REPO_ROOT}/tests/ui-scripts/shell"
    UI_GOLDEN_DIR="${REPO_ROOT}/tests/golden/ui/shell"
    UI_GOT_DIR="${CMAKE_BINARY_DIR}/got")
set_target_properties(deluge_scenes_spec PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
