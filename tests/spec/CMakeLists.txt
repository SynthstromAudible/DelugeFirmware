include(FetchContent)
FetchContent_Declare(CppSpec
   GIT_REPOSITORY https://github.com/toroidal-code/cppspec/
   GIT_TAG v1.0.0
   GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CppSpec)

add_subdirectory(../../src/lib printf)

add_library(deluge_spec STATIC
../../src/deluge/util/semver.cpp
../../src/deluge/util/string.cpp
../../src/deluge/util/cfunctions.c
../../src/deluge/util/lookuptables/lookuptables.cpp
../32bit_unit_tests/mocks/mock_display.cpp
../32bit_unit_tests/mocks/mock_print.cpp
)
target_link_libraries(deluge_spec PUBLIC eyalroz_printf)

target_include_directories(deluge_spec PUBLIC ../../src/deluge ../../src ../32bit_unit_test/mocks/)

set_target_properties(deluge_spec PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
)
link_libraries(deluge_spec)
discover_specs(${CMAKE_CURRENT_SOURCE_DIR} TRUE)

