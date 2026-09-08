# tests/spec_io/CMakeLists.txt
#
# deluge::io (idiomatic C++ wrapper over libdeluge/file_io.h) specs. Links the
# wrapper against a hand-written in-memory mock of file_io.h's C-ABI (mock_file_io.cpp),
# NOT the real FatFS-backed adapter (that's tests/spec/'s concern, for the adapter's
# own tests) -- so these specs get real open/read/write/seek/close and
# directory-iteration round-trip coverage with no mounted filesystem required.
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

function(create_specs_driver driver_name spec_dir)
  # CONFIGURE_DEPENDS so a newly added *_spec.cpp is picked up without a manual reconfigure.
  # Without it `./dbt test` — which only configures when build/tests is absent — compiles the new
  # spec into nothing and reports a green run, so a spec that was never built looks exactly like a
  # spec that passed.
  file(GLOB_RECURSE spec_sources CONFIGURE_DEPENDS RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name}
    ${specs}
    mock_file_io.cpp
    ../../src/deluge/io/file.cpp
    ../../src/deluge/storage/existence_policy.cpp
    ../../src/deluge/storage/startup_song_policy.cpp
    ../../src/deluge/model/sample/recorder_write_policy.cpp
    ../../src/deluge/model/sample/recorder_finalize_policy.cpp
    ../../src/deluge/model/sample/recorder_create_policy.cpp
  )
  target_link_libraries(${driver_name} PRIVATE c++spec)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ../../include      # libdeluge/file_io.h, libdeluge/types.h
    ../../src/deluge    # io/file.hpp
    ../../src           # definitions_cxx.hpp (storage/storage_manager.h's transitive include)
    ../../src/OSLikeStuff # scheduler_api.h (libdeluge/scheduler.h's transitive include)
    ../32bit_unit_tests/mocks # util/firmware_version.h host stub (real one is a .h.in template, generated
                               # at firmware-build time; other spec targets already rely on this same mock)
  )
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
  )
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(io_specs ${CMAKE_CURRENT_LIST_DIR})
