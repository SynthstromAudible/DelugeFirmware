add_subdirectory(../../../src/lib printf)
add_subdirectory(../../../src/midi deluge_midi)

add_library(deluge_spec STATIC
# Audio-file header parser + its dependency-free byte/format helpers (file-loading regression net).
../../../src/deluge/storage/audio/audio_file_format.cpp
../../../src/deluge/storage/build_value.cpp
../../../src/deluge/storage/value_tree_deserializer.cpp
../../../src/deluge/util/audio_format_helpers.cpp
../../../src/deluge/util/string.cpp
# intToString/floatToString moved out of util/cfunctions.c into the dep-free
# foundation library; cfunctions.c now only holds the clock-coupled delays.
../../../src/foundation/string_format.c
../../../src/deluge/util/c_string.cpp
../../../src/deluge/util/lookuptables/lookuptables.cpp
../../support/mocks/mock_display.cpp
../../support/mocks/mock_print.cpp
../../support/mocks/mock_firmware_version.cpp
# ObjectPool guards its free list with CriticalSectionGuard, whose C ABI the BSP owns; the
# specs have no BSP, and single-threaded drivers have nothing to mask.
../../support/mocks/mock_critical_section.cpp
# The pad-grid crash pattern, against fault_pattern_spec.cpp's recording board.
../../../src/deluge/io/debug/fault_pattern.c
# The UI layers' code under test that is not in their own libraries: the guest's surfaces and host UI against fakes of
# the legacy symbols they touch, the hid button decode, the menu node ids and validator (model-free), and the level-0
# and level-3 harness writers and tables.
../../../src/deluge/hid/display/oled_canvas/canvas.cpp
../../../src/deluge/gui/ui/notify_drain_mapping.cpp
../../../src/deluge/gui/ui/shell_guest_surfaces.cpp
gui/support/fake_legacy_hid.cpp
../../../src/deluge/gui/ui/shell_host_ui.cpp
gui/support/fake_legacy_ui.cpp
../../../src/deluge/hid/button.cpp
../../../src/scenes/menu/node_id.cpp
../../../src/scenes/menu/validate.cpp
scenes/support/fake_shortcuts.cpp
scenes/support/fake_bindings_relevant.cpp
../../harness/ui_script.cpp
../../harness/snapshot.cpp
../../harness/level3/host_script_tables.c
../../harness/level3/host_snapshot.c
)
# deluge_ui_oled: the starfield and rainfall animations the specs and previews drive.
target_link_libraries(deluge_spec PUBLIC eyalroz_printf deluge_midi deluge_serialization deluge_ui_oled
  deluge_gfx_oled deluge_gfx_grid deluge_ui_core deluge_ui_grid deluge_ui_shell deluge_notify)

# audio_file_format.cpp's header parse IS the Rust decoder (crates/deluge_audio_format, reached through
# include/libdeluge/audio_format.h), so these specs need its C ABI on the link line. `deluge_rust_rs` is
# the umbrella staticlib defined once in tests/unit/CMakeLists.txt -- i686 on Linux, host-native on macOS.
# This subdirectory is added unconditionally, so fail loudly on a platform that defines neither rather
# than at link time with six undefined `deluge_audio_header_*` symbols.
if(NOT TARGET deluge_rust_rs)
    message(FATAL_ERROR "tests/unit/spec needs the deluge_rust staticlib (deluge_rust_rs), which "
                        "tests/unit/CMakeLists.txt only defines for Linux and macOS.")
endif()
target_link_libraries(deluge_spec PUBLIC deluge_rust_rs)

target_include_directories(deluge_spec PUBLIC ../../../src/deluge ../../../src ../../support/mocks/ ../../../include)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)

# The trees the UI specs read and write: gfx and level-2 goldens, the level-2 scripts, and the scratch the runner writes
# its snapshots into before diffing them.
target_compile_definitions(deluge_spec PUBLIC
  GFX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../../golden/gfx"
  DELUGE_REPO_ROOT="${CMAKE_CURRENT_LIST_DIR}/../../.."
  UI_SCRIPTS_DIR="${CMAKE_CURRENT_LIST_DIR}/../../ui-scripts/shell"
  UI_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../../golden/ui/shell"
  UI_GOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/got"
)
# tests/ for "harness/..." and tests/unit/ for "spec/support/...".
target_include_directories(deluge_spec PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_LIST_DIR}/..)

deluge_specs_driver(all_specs INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../.. ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(all_specs PUBLIC deluge_spec deluge_midi)
deluge_apply_test_sanitizers(all_specs)

# Visual harness for the OLED starfield. Renders frames as ASCII so the ported
# animation can be checked by eye without hardware. Deliberately has no add_test:
# it asserts nothing, and its filename avoids the *_spec.cpp glob.
add_executable(StarfieldPreview starfield_preview.cpp)
target_link_libraries(StarfieldPreview PRIVATE deluge_spec)
set_target_properties(StarfieldPreview PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
)

# Visual harness for the OLED rain screensaver, on the same terms as StarfieldPreview above.
add_executable(RainfallPreview rainfall_preview.cpp)
target_link_libraries(RainfallPreview PRIVATE deluge_spec)
set_target_properties(RainfallPreview PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
)
