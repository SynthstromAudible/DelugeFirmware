cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(DelugeTestsArm)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deps.cmake)

set(CPU_OPTIONS
  -mcpu=cortex-a9
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
add_link_options(${CPU_OPTIONS})

enable_testing()
add_subdirectory(spec)
