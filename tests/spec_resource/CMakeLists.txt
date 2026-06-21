#
# deluge_resource (Rust cached-asset manager) conformance specs (CppSpec).
#
# Links the shared deluge_rust umbrella staticlib (built once in ../CMakeLists.txt,
# i686 to match the -m32 test build) and drives the resource manager through its C
# ABI (crates/deluge_resource/include/deluge_resource.h) with a mock Source.
#
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_link_libraries(${driver_name} PRIVATE c++spec deluge_rust_rs
    # System libs the Rust std staticlib references.
    pthread dl m)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../crates/deluge_resource/include # deluge_resource.h
    ${CMAKE_CURRENT_LIST_DIR}/../../include                        # libdeluge/alloc.h
  )
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
  )
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(resource_specs ${CMAKE_CURRENT_LIST_DIR})
