cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# This tree only ever cross-builds for ARM Linux, run under qemu: with clang, as the firmware is built.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-linux-clang.cmake)
endif()
project(DelugeTestsArm)
# The DX7 asm pin links a test-local copy of the device's NEON FM kernel.
enable_language(ASM)

set(CPU_OPTIONS
  -mcpu=cortex-a9
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
add_link_options(${CPU_OPTIONS})

include(${CMAKE_CURRENT_LIST_DIR}/../support/cmake/specs.cmake)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../serialization serialization)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../music-theory music-theory)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../bench bench)
add_subdirectory(spec)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../plugin-api plugin-api)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../engines engines)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../model model)

# dsp-next specs on real ARM NEON (run via qemu). DSP_SPEC_ARM tells the shared
# tests/dsp build to skip the x86/SIMDe setup. Same specs as the x86 test project.
set(DSP_SPEC_ARM TRUE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../dsp dsp)
