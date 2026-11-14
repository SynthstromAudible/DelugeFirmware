# CppUTest's UnitTests executable, which only the host tree builds.
deluge_test_directory(TREES host)

# Set this to ON if you want to have the CppUTest's internal tests in your
# project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
# Memory leak detection is off by default on Windows; setting it there causes a flood of redefinition warnings.
if (NOT WIN32)
    add_compile_definitions(CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
endif ()

FetchContent_MakeAvailable(CppUTest)

add_executable(UnitTests
        RunAllTests.cpp
)
# No ctest test runs it: it holds no tests, and CppUTest fails a run that finds none.

target_link_libraries(UnitTests PRIVATE CppUTestExt)

deluge_apply_test_sanitizers(UnitTests)
