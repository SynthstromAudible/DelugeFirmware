# CppUTest
include(FetchContent)
FetchContent_Declare(
    CppUTest
    GIT_REPOSITORY https://github.com/cpputest/cpputest.git
    GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)

# Set this to ON if you want to have the CppUTests in your project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
add_compile_definitions(
    CPPUTEST_MEM_LEAK_DETECTION_DISABLED
    IN_UNIT_TESTS=1
)

FetchContent_MakeAvailable(CppUTest)

file(GLOB_RECURSE deluge_SOURCES

    # Required for memory tests
    ../../src/deluge/memory/*

    # Used for prints
    ../../src/deluge/gui/l10n/*

    # Used by most other modules
    ../../src/deluge/util/*

    # Mock implementations
    mocks/*
)

add_executable(RunAllTests RunAllTests.cpp memory_tests.cpp)
target_sources(RunAllTests PRIVATE ${deluge_SOURCES})
target_include_directories(RunAllTests PRIVATE

    # include the non test project source
    mocks
    ../../src/deluge
    ../../src/NE10/inc
    ../../src
)

set_target_properties(RunAllTests
    PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        LINK_FLAGS -m32
)

target_link_libraries(RunAllTests CppUTest CppUTestExt)

# strchr is seemingly different in x86
target_compile_options(RunAllTests PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)
