# tests/unit/storage/CMakeLists.txt
#
# deluge::storage specs: the header-only seams under src/deluge/storage/.
include(FetchContent)
FetchContent_MakeAvailable(CppSpec)

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name}
    ${specs}
  )
  target_link_libraries(${driver_name} PRIVATE c++spec)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ../../../src/deluge    # storage/*.h
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

create_specs_driver(storage_specs ${CMAKE_CURRENT_LIST_DIR})
