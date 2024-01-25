cmake_minimum_required(VERSION 3.24)
project (DelugeTests)
add_compile_options(
    -m32
    -Og
    -g
    -ggdb
)
# CppUTest
include(FetchContent)
FetchContent_Declare(
    CppUTest
    GIT_REPOSITORY https://github.com/cpputest/cpputest.git
    GIT_TAG        eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)
# Set this to ON if you want to have the CppUTests in your project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
add_compile_definitions(
    CPPUTEST_MEM_LEAK_DETECTION_DISABLED
    IN_UNIT_TESTS=1
)

FetchContent_MakeAvailable(CppUTest)
#strchr is seemingly different in x86
add_compile_options(
    -fpermissive
)

# include the non test project source
include_directories(./mocks)
include_directories(../src/deluge)
include_directories(../src/NE10/inc)
include_directories(../src)


file(GLOB_RECURSE deluge_SOURCES
  # Required for memory tests
  ../src/deluge/memory/*
  # Used for prints
  ../src/deluge/gui/l10n/*
  # Used by most other modules
  ../src/deluge/util/*
  # Mock implementations
  mocks/*
)



add_executable(RunAllTests RunAllTests.cpp memory_tests.cpp)
target_sources(RunAllTests PUBLIC ${deluge_SOURCES})

set_target_properties(RunAllTests
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        LINK_FLAGS -m32
)

target_link_libraries(RunAllTests CppUTest CppUTestExt)
