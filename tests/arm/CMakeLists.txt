cmake_minimum_required(VERSION 3.24)
project(DelugeTestsArm)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
  set(CPU_OPTIONS
    -mcpu=cortex-a9
    -mfloat-abi=hard
    -mfpu=neon
  )
endif()
add_compile_options(${CPU_OPTIONS} -Wno-psabi -static)
add_link_options(${CPU_OPTIONS})

enable_testing()
add_subdirectory(spec)

