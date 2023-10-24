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
    GIT_TAG        master # or use release tag, eg. v3.8
)
# Set this to ON if you want to have the CppUTests in your project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
add_compile_definitions(
    -DCPPUTEST_MEM_LEAK_DETECTION_DISABLED
    -DIN_UNIT_TESTS
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


#memory because I'm starting with a memory test
#util because they're needed all over the place
#l10n so the prints make sense
file(GLOB_RECURSE deluge_SOURCES ../src/deluge/memory/* ../src/deluge/util/* mocks/* ../src/deluge/gui/l10n/*)



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
