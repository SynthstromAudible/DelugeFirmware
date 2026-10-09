add_subdirectory(../../../src/lib printf)
add_subdirectory(../../../src/midi deluge_midi)

add_library(deluge_spec STATIC
# Audio-file header parser + its dependency-free byte/format helpers (file-loading regression net).
../../../src/deluge/storage/audio/audio_file_format.cpp
../../../src/deluge/util/audio_format_helpers.cpp
../../../src/deluge/util/semver.cpp
../../../src/deluge/util/string.cpp
# intToString/floatToString moved out of util/cfunctions.c into the dep-free
# foundation library; cfunctions.c now only holds the clock-coupled delays.
../../../src/foundation/string_format.c
../../../src/deluge/util/c_string.cpp
../../../src/deluge/util/lookuptables/lookuptables.cpp
../../../src/deluge/hid/display/starfield.cpp
../../../src/deluge/hid/display/rainfall.cpp
../../support/mocks/mock_display.cpp
../../support/mocks/mock_print.cpp
# ObjectPool guards its free list with CriticalSectionGuard, whose C ABI the BSP owns; the
# specs have no BSP, and single-threaded drivers have nothing to mask.
../../support/mocks/mock_critical_section.cpp
# The pad-grid crash pattern, against fault_pattern_spec.cpp's recording board.
../../../src/deluge/io/debug/fault_pattern.c
)
target_link_libraries(deluge_spec PUBLIC eyalroz_printf deluge_midi)

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

deluge_specs_driver(all_specs)
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
