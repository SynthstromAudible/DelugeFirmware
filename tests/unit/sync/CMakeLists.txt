# tests/unit/sync/CMakeLists.txt
#
# deluge::sync specs. Exercises the RAII scope primitives directly against the real
# storage_op.cpp translation unit (which owns the relocated permit-flag definition) --
# no card, no FatFS, no resource manager.
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name}
    ${specs}
    ../../../src/deluge/sync/storage_op.cpp
    ../../../src/deluge/sync/sd_access.cpp
  )
  target_link_libraries(${driver_name} PRIVATE c++spec)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ../../../include      # libdeluge/types.h
    ../../../src/deluge    # sync/storage_op.h
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

create_specs_driver(sync_specs ${CMAKE_CURRENT_LIST_DIR})
