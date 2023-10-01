cmake_minimum_required(VERSION 3.24)
project (DelugeTests)
add_compile_options(
    -m32
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
add_compile_definitions(-DCPPUTEST_MEM_LEAK_DETECTION_DISABLED)
FetchContent_MakeAvailable(CppUTest)
#strchr is seemingly different in x86
add_compile_options(
    -fpermissive
)

# include the non test project source
include_directories(../src/deluge)
include_directories(../src)
include_directories(./mocks)
file(GLOB_RECURSE deluge_SOURCES ../src/deluge/memory/* ../src/deluge/util/*)

#list(FILTER deluge_SOURCES EXCLUDE REGEX ".*point\\.h$")
#target_sources(deluge PUBLIC ${deluge_SOURCES})

add_executable(RunAllTests RunAllTests.cpp memory_tests.cpp ${deluge_SOURCES})


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
