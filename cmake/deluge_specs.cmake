# CppSpec fetch + create_specs_driver(driver_name spec_dir): the shared spec-executable machinery
# both the `tests/` project (tests/spec/CMakeLists.txt's `all_specs`) and the `sim/` project
# (sim/CMakeLists.txt's `deluge_scenes_spec`) build against. Factored out verbatim from
# tests/spec/CMakeLists.txt (task 1, spec E1) so a model-backed spec executable can live in
# sim/ -- linking the real `deluge_app` rather than tests/'s mocks -- without duplicating this.
#
# Include once per project, before calling create_specs_driver():
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deluge_specs.cmake)   # from tests/spec/
#   include(${REPO_ROOT}/cmake/deluge_specs.cmake)                     # from sim/

include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

function(create_specs_driver driver_name spec_dir)
  # Discover Specs
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)

  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec)
  # tests/ root: support/script_runner.h and harness/{ui_script,identity}_spec.cpp
  # #include "harness/ui_script.h" / "harness/snapshot.h" (tests/harness's dependency-free level-0
  # parser and snapshot writer). Granted directly to the driver that compiles those files (M-9),
  # rather than left on deluge_spec's PUBLIC include path, so StarfieldPreview/RainfallPreview --
  # which also link deluge_spec, but need none of this -- do not inherit golden/, fixtures/,
  # ui-scripts/ and spec/ along with it.
  target_include_directories(${driver_name} PRIVATE ${spec_dir}/..)
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
