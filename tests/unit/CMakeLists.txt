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
        # Mock implementations
        mocks/*
)

add_executable(UnitTests RunAllTests.cpp scheduler_tests.cpp)
target_sources(UnitTests PRIVATE ${deluge_SOURCES})
target_include_directories(UnitTests PRIVATE
        # include the non test project source
        mocks
        ../../src
        ../../src/deluge
)

set_target_properties(UnitTests
        PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(UnitTests CppUTestExt)

# strchr is seemingly different in x86
target_compile_options(UnitTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)
