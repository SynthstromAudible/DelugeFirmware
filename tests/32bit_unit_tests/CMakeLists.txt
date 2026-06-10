#
# CppUTest
#
include(FetchContent)
FetchContent_Declare(
        CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)

set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")

add_compile_definitions(
        CPPUTEST_MEM_LEAK_DETECTION_DISABLED
        IN_UNIT_TESTS=1
)

FetchContent_MakeAvailable(CppUTest)

#
# Find source files
#

file(GLOB_RECURSE deluge_mm_SOURCES
        # Required for memory tests
        ../../src/deluge/memory/*
)

file(GLOB_RECURSE deluge_SOURCES
        # Used for prints
        ../../src/deluge/gui/l10n/*

        # Used by most other modules
        ../../src/deluge/util/*
        ../../src/lib/printf.c
)

# intToString/floatToString moved from util/cfunctions.c into the dep-free
# foundation library; pull the formatting source in so the tests still link.
list(APPEND deluge_SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../src/foundation/string_format.c)

file(GLOB_RECURSE mock_SOURCES
        # Mock implementations
        mocks/*
)

#
# Build the memory manager test binary, and register it with ctest.
#
add_executable(MemoryManagerTests RunAllTests.cpp memory_tests.cpp)

add_test(NAME MemoryManagerTests COMMAND MemoryManagerTests)
target_sources(MemoryManagerTests PRIVATE ${deluge_mm_SOURCES} ${deluge_SOURCES} ${mock_SOURCES})

target_include_directories(MemoryManagerTests PRIVATE
        # include the non test project source
        mocks
        ../../src/deluge
        ../../src/OSLikeStuff
        ../../src/NE10/inc
        ../../src/lib
        ../../src
        ../../include    # libdeluge/* boundary headers
        ../../src/bsp/rza1 # relocated drivers/* headers
)

set_target_properties(MemoryManagerTests
        PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        LINK_FLAGS -m32
)

target_link_libraries(MemoryManagerTests CppUTest CppUTestExt)

# strchr is seemingly different in x86
target_compile_options(MemoryManagerTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)

#
# Build tests for all the code that assumes 32-bit pointers.
#
add_executable(SmallPointerTests
        RunAllTests.cpp
        container/open_addressing_hash_table.cpp
)

add_test(NAME SmallPointerTests COMMAND SmallPointerTests)
target_sources(SmallPointerTests PRIVATE
        ${deluge_SOURCES}
        ${mock_SOURCES}
        ./mock_memory_manager.cpp)
target_include_directories(SmallPointerTests PRIVATE
        # include the non test project source
        mocks
        ../../src/deluge
        ../../src/OSLikeStuff # libdeluge/scheduler.h forwards to scheduler_api.h here
        ../../src/NE10/inc
        ../../src/lib
        ../../src
        ../../include    # libdeluge/* boundary headers
        ../../src/bsp/rza1 # relocated drivers/* headers
)

set_target_properties(SmallPointerTests
        PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        LINK_FLAGS -m32
)

target_link_libraries(SmallPointerTests CppUTest CppUTestExt)

# strchr is seemingly different in x86
target_compile_options(SmallPointerTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)
