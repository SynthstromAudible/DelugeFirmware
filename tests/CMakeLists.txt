cmake_minimum_required(VERSION 3.24)
project (DelugeTests)
# Check for CppUTest and bail out if they don't have it
if(DEFINED ENV{CPPUTEST_HOME})
  message("Using CppUTest found in $ENV{CPPUTEST_HOME}")
else()
  message("CPPUTEST_HOME is not set; You must tell CMake where to find CppUTest")
  return()
endif()


# include the non test project source
include_directories(../src)

# include CppUTest headers
include_directories($ENV{CPPUTEST_HOME}/include)

# add cpputest library
add_library(imp_cpputest STATIC IMPORTED)
set_property(TARGET imp_cpputest PROPERTY
             IMPORTED_LOCATION $ENV{CPPUTEST_HOME}/lib/libCppUTest.a)

# build test library for LedDriver
add_library(demo_test demo_test.cpp)

add_executable(RunAllTests RunAllTests.cpp)
target_link_libraries(RunAllTests imp_cpputest demo_test)
