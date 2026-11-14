# Gate unit-arm:
#     the fixed-point, VFP and NEON specs must hold on ARM, cross-built and run under qemu: the NEON conversion kernels
#     must match their scalar references bit for bit, every fixed-point operation (and its portable twin) must match the
#     A9 instruction it stands for, and every fixed-point operation must compile, at the shipping image's ARM and
#     Thumb-2 codegen flags, to the instruction it names (tests/unit-arm/codegen)

cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# This tree only ever cross-builds for ARM Linux, run under qemu: with clang, as the firmware is built.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../cmake/arm-linux-clang.cmake)
endif()
project(DelugeTestsArm)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/ccache.cmake)
# The DX7 asm pin links a test-local copy of the device's NEON FM kernel.
enable_language(ASM)

set(CPU_OPTIONS
  -mcpu=cortex-a9
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
add_link_options(${CPU_OPTIONS})

set(DELUGE_TEST_TREE arm)
include(${CMAKE_CURRENT_LIST_DIR}/../support/cmake/specs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../support/cmake/libs.cmake)

enable_testing()
add_subdirectory(spec)
add_subdirectory(codegen)
# Every test directory under tests/unit that names this tree (tests/support/cmake/specs.cmake).
deluge_spec_directories(${DELUGE_REPO_ROOT}/tests/unit)

# dsp-next specs on real ARM NEON (run via qemu). DSP_SPEC_ARM tells the shared
# tests/dsp build to skip the x86/SIMDe setup. Same specs as the x86 test project.
set(DSP_SPEC_ARM TRUE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../dsp dsp)
