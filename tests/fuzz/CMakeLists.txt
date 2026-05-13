cmake_minimum_required(VERSION 3.24)
project(DelugeFuzz LANGUAGES C CXX)

# libFuzzer is only available with clang. The harnesses link against
# real source from src/deluge/, so this directory is opt-in. To build
# standalone from the repository root:
#
#   CC=clang CXX=clang++ cmake -B build/fuzz -S tests/fuzz
#   cmake --build build/fuzz
#
# Then run any harness:
#
#   ./build/fuzz/pack_fuzz   -max_len=8192
#   ./build/fuzz/semver_fuzz -max_len=128
#   ./build/fuzz/memmove_fuzz -max_len=512

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "Fuzz harnesses require clang (for -fsanitize=fuzzer). "
        "Re-run cmake with CC=clang CXX=clang++.")
endif()

set(fuzz_flags
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
    -O1
)

function(add_fuzz_target name)
    set(srcs ${ARGN})
    add_executable(${name} ${srcs})
    target_compile_options(${name} PRIVATE ${fuzz_flags})
    target_link_options(${name} PRIVATE ${fuzz_flags})
endfunction()

# Existing harness for the in-tree memmove implementation.
add_fuzz_target(memmove_fuzz memmove_fuzz.c)
target_compile_features(memmove_fuzz PUBLIC c_std_23)

# SysEx 7-bit pack / unpack / RLE. Pure C, no project deps beyond pack.{c,h}.
add_fuzz_target(pack_fuzz pack_fuzz.c ../../src/deluge/util/pack.c)
target_compile_features(pack_fuzz PUBLIC c_std_23)

# Firmware version-string parser. Pure C++, stdlib-only deps.
add_fuzz_target(semver_fuzz semver_fuzz.cpp ../../src/deluge/util/semver.cpp)
target_compile_features(semver_fuzz PUBLIC cxx_std_23)
target_include_directories(semver_fuzz PRIVATE ../../src/deluge)
