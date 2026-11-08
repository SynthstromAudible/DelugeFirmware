cmake_minimum_required(VERSION 3.24)
project(DelugeTests)

if (WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Gcc")
    # CPPUTEST_PLATFORM defaults to Gcc, which causes the test binaries to
    # segfault on Windows/Clang builds, since they use the stdlib from MSVC.
    set(CPPUTEST_PLATFORM VisualCpp)
endif ()

set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 23)

enable_testing()

include(FetchContent)

# The DSP headers are written against Argon. Keep this tag in sync with lib/CMakeLists.txt.
FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 4b8efc4f4f21595d454e415fb619dd91223c0400
)
FetchContent_MakeAvailable(argon)

# Argon falls back to SIMDe's <arm/neon.h> shim when the host isn't ARM, which is
# what lets the DSP headers build (and be specced) on a normal x86 dev machine.
FetchContent_Declare(simde
  GIT_REPOSITORY https://github.com/simd-everywhere/simde
  GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(simde)

add_compile_options(-Wno-unknown-warning-option)
#needs to support -m32 since the memory tests assume a 32 bit architecture
if (UNIX AND NOT APPLE)
    add_compile_options(-m32 -Og -ggdb3)
    add_link_options(-m32)
    add_subdirectory(32bit_unit_tests)
endif ()
add_subdirectory(oled_handshake)
add_subdirectory(spec)
add_subdirectory(unit)
add_subdirectory(hardware)
