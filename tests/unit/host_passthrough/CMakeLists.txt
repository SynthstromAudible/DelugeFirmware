# tests/unit/host_passthrough/CMakeLists.txt
#
# Specs for the C-host POSIX passthrough (src/bsp/host/host_efatfs_passthrough.cpp): the strong
# deluge_efatfs_* definitions the sim links, tested directly against a temp dir under DELUGE_SD_ROOT.
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

file(GLOB_RECURSE spec_sources RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
create_test_sourcelist(specs host_passthrough_specs.cpp ${spec_sources})
add_executable(host_passthrough_specs
  ${specs}
  ../../../src/bsp/host/host_efatfs_passthrough.cpp
)
target_link_libraries(host_passthrough_specs PRIVATE c++spec)
target_include_directories(host_passthrough_specs PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ../../../include      # libdeluge/file_io.h, stream_io.h, streaming_fill.h
)
set_target_properties(host_passthrough_specs PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
foreach(spec IN LISTS spec_sources)
  cmake_path(GET spec STEM LAST_ONLY spec_name)
  add_test(NAME ${spec_name} COMMAND host_passthrough_specs ${spec_name} --verbose)
endforeach()
