# Gate unit-arm:
#     the DSP library, NEON, model, effects, instrument, music, serialization, plugin, host-chain and
#     bench specs and model equality hold on ARM, cross-built with clang and run under qemu: tests/unit/dsp
#     and the test directories under tests/unit that name this tree, the fixed-point and VFP specs among them. The NEON
#     conversion kernels must match their scalar references bit for bit, every fixed-point operation (and its portable
#     twin) must match the A9 instruction it stands for, and every fixed-point operation must compile, at the shipping
#     image's ARM and Thumb-2 codegen flags, to the instruction it names (tests/unit/dsp/core);
#     model_equality_synthetic reads the synthetic songs and the model records on ARM as on the host

cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# This tree only ever cross-builds for ARM Linux, run under qemu: with clang, as the firmware is built.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../../cmake/arm-linux-clang.cmake)
endif()
project(DelugeTestsArm)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/ccache.cmake)
# The DSP library's DX7 kernel (fixed/dx7/neon_fm_kernel.s) and the dsp specs' test-local copy of it are assembled here.
enable_language(ASM)

set(CPU_OPTIONS
  -mcpu=cortex-a9
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
# Assembly is preprocessed, as the device assembles it, so the compile definitions and include paths a library gives
# its sources apply to its .s files too instead of failing the libraries' -Werror as unused arguments.
add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-x> $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>)
add_link_options(${CPU_OPTIONS})

set(DELUGE_TEST_TREE arm)
include(${CMAKE_CURRENT_LIST_DIR}/../../support/cmake/specs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../support/cmake/libs.cmake)

enable_testing()
# Every test directory under tests/unit that names this tree (tests/support/cmake/specs.cmake).
deluge_spec_directories(${DELUGE_REPO_ROOT}/tests/unit)
