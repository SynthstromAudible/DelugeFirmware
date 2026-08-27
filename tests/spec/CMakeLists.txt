include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

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
)
target_link_libraries(deluge_spec PUBLIC eyalroz_printf deluge_midi deluge_gfx_oled deluge_gfx_grid deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_notify)

target_include_directories(deluge_spec PUBLIC ../../src/deluge ../../src ../32bit_unit_test/mocks/ ../../include)

target_compile_definitions(deluge_spec PUBLIC GFX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/gfx")

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)

function(create_specs_driver driver_name spec_dir)
  # Discover Specs
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)

  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec)
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
  )

  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    # create_test_sourcelist registers each spec under its relative path minus extension
    # (e.g. "gfx/canvas_golden_spec"), not just the file stem, whenever the spec lives in a
    # subdirectory. Pass that full id as the driver argument so name matching succeeds, while
    # keeping the ctest test NAME as the bare stem for a short `-R <name>` filter.
    cmake_path(REMOVE_EXTENSION spec OUTPUT_VARIABLE spec_id)
    add_test(
      NAME ${spec_name}
      COMMAND ${driver_name} ${spec_id} --verbose
    )
  endforeach()
endfunction()

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
