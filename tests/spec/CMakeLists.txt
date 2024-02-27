include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

add_library(deluge_spec STATIC
  ../../src/deluge/util/semver.cpp
)

target_include_directories(deluge_spec PUBLIC ../../src/deluge)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
)

function(create_specs_driver driver_name spec_dir)
  # Discover Specs
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)

  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec)
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
  )

  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(
      NAME ${spec_name}
      COMMAND ${driver_name} ${spec_name} --verbose
    )
  endforeach()
endfunction()

create_specs_driver(all_specs ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(all_specs PUBLIC deluge_spec)
