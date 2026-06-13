cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(DelugeTestsArm)

set(CPU_OPTIONS
  -mcpu=cortex-a9
  -mfloat-abi=hard
  -mfpu=neon
)
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
add_link_options(${CPU_OPTIONS})

enable_testing()
add_subdirectory(spec)

# dsp-next specs on real ARM NEON (run via qemu). DSP_SPEC_ARM tells the shared
# tests/dsp build to skip the x86/SIMDe setup. Same specs as the x86 test project.
set(DSP_SPEC_ARM TRUE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../dsp dsp)
