include(FetchContent)
FetchContent_Declare(CppSpec
   GIT_REPOSITORY https://github.com/toroidal-code/cppspec/
   GIT_TAG v1.0.0
   GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CppSpec)

add_library(deluge_spec INTERFACE)

target_include_directories(deluge_spec INTERFACE
  ../../../src/deluge
  ../../../src
  ../../32bit_unit_test/mocks/
)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
)

discover_specs(${CMAKE_CURRENT_SOURCE_DIR} TRUE)
target_link_libraries(all_specs PUBLIC deluge_spec)
