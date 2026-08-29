# CppSpec fetch + create_specs_driver(): shared with sim/CMakeLists.txt (task 1, spec E1).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deluge_specs.cmake)

add_subdirectory(../../src/lib printf)
add_subdirectory(../../src/midi deluge_midi)

add_library(deluge_spec STATIC
# Audio-file header parser + its dependency-free byte/format helpers (file-loading regression net).
../../src/deluge/storage/audio/audio_file_format.cpp
../../src/deluge/util/audio_format_helpers.cpp
../../src/deluge/util/semver.cpp
../../src/deluge/util/string.cpp
# intToString/floatToString moved out of util/cfunctions.c into the dep-free
# foundation library; cfunctions.c now only holds the clock-coupled delays.
../../src/foundation/string_format.c
../../src/deluge/util/c_string.cpp
../../src/deluge/util/lookuptables/lookuptables.cpp
../../src/deluge/hid/display/starfield.cpp
../../src/deluge/hid/display/rainfall.cpp
../../src/deluge/hid/display/oled_canvas/canvas.cpp
../../src/deluge/gui/ui/notify_drain_mapping.cpp
../32bit_unit_tests/mocks/mock_display.cpp
../32bit_unit_tests/mocks/mock_print.cpp
# UI test harness level 0: script parser + .state formatter (dependency-free; also compiled
# into the host BSP, so it may not include src/shell -- see tests/harness/README.md).
../harness/ui_script.cpp
../harness/snapshot.cpp
# UI test harness level 3's name tables (dependency-free; also compiled into the host BSP) --
# tests/spec/harness/tables_spec.cpp cross-checks these against deluge_profile() so the level-3
# copy of the button matrix / encoder order cannot silently drift. See
# src/bsp/host/host_script_tables.h for why this is its own translation unit.
../../src/bsp/host/host_script_tables.c
# UI test harness level 3's .pbm/.state writers (dependency-free; also compiled into the host
# BSP), split out the same way (final review I-3) so tests/spec/harness/identity_spec.cpp can
# cross-check this C port against tests/harness/snapshot.cpp. See src/bsp/host/host_snapshot.h.
../../src/bsp/host/host_snapshot.c
)
target_link_libraries(deluge_spec PUBLIC eyalroz_printf deluge_midi deluge_gfx_oled deluge_gfx_grid deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_ui_shell deluge_notify)

target_include_directories(deluge_spec PUBLIC ../../src/deluge ../../src ../32bit_unit_test/mocks/ ../../include)
# ".." (tests/, PRIVATE, not PUBLIC): needed only by this library's OWN
# src/bsp/host/host_snapshot.c, whose `#include "harness/ui_script.h"` matches the host BSP's own
# include convention (host_script.c uses the same spelling). Kept off deluge_spec's public
# interface so the two preview executables that link it (StarfieldPreview, RainfallPreview) do
# not also inherit golden/, fixtures/, ui-scripts/ and spec/ along with it (M-9) -- the one thing
# actually needing this on its own include path is the *_spec.cpp files compiled into
# create_specs_driver's ${driver_name}, which is granted it directly, below.
target_include_directories(deluge_spec PRIVATE ..)

# DELUGE_REPO_ROOT: the repo root, for specs that shell out to scripts/ (e.g.
# harness/snapshot_tools_spec.cpp, which runs scripts/ui_snapshot_diff.py and
# scripts/pbm2png.py via std::system). Derived the same way as GFX_GOLDEN_DIR above.
# UI_SCRIPTS_DIR / UI_GOLDEN_DIR / UI_GOT_DIR: the level-2 script harness's three trees -- the
# committed scripts, the committed goldens, and the build-tree scratch the runner writes its
# snapshots into before diffing them (see tests/spec/shell/support/script_runner.h).
target_compile_definitions(deluge_spec PUBLIC
  GFX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/gfx"
  DELUGE_REPO_ROOT="${CMAKE_CURRENT_LIST_DIR}/../.."
  UI_SCRIPTS_DIR="${CMAKE_CURRENT_LIST_DIR}/../ui-scripts/shell"
  UI_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/ui/shell"
  UI_GOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/got"
)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)

create_specs_driver(all_specs ${CMAKE_CURRENT_LIST_DIR})
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
