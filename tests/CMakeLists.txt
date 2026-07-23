cmake_minimum_required(VERSION 3.24)
project(DelugeTests)

if (WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Gcc")
    # CPPUTEST_PLATFORM defaults to Gcc, which causes the test binaries to
    # segfault on Windows/Clang builds, since they use the stdlib from MSVC.
    set(CPPUTEST_PLATFORM VisualCpp)
endif ()

set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 23)

# Opt-in dynamic analysis for the host test binaries. Enable with -DDELUGE_TEST_SANITIZERS=ON.
#
# AddressSanitizer (heap/stack overflow, use-after-free, double-free) and UndefinedBehaviorSanitizer both work in the
# -m32 host build (the i386 libasan/libubsan runtimes are required - on Arch that's `lib32-gcc-libs`). NOTE: LeakSanitizer
# (ASan's leak-at-exit detector) is x86-64 only and silently does nothing under -m32, which is why host leak checking is
# done explicitly via the mock allocator's live-allocation counter (see mock_memory_manager.cpp) rather than via LSan.
option(DELUGE_TEST_SANITIZERS "Build host tests with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Apply the sanitizers to a single test target only. We deliberately do NOT instrument globally: the CppUTest / CppSpec /
# etl dependencies are pulled in via FetchContent and have their own benign UB (e.g. CppUTest's millis*1000 timing
# overflow) that would otherwise halt every run. ASan's runtime intercepts malloc/free process-wide regardless, so the
# uninstrumented framework objects are still covered for heap errors.
function(deluge_apply_test_sanitizers target)
    if (DELUGE_TEST_SANITIZERS AND UNIX AND NOT APPLE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif ()
endfunction()

enable_testing()
add_compile_options(-Wno-unknown-warning-option)
#needs to support -m32 since the memory tests assume a 32 bit architecture
if (UNIX AND NOT APPLE)
    # -mfpmath=sse -msse2: do double math in SSE (64-bit IEEE) rather than the x87 stack (80-bit). x87 excess precision
    # makes exact float comparisons depend on when intermediates spill to memory, which sanitizer instrumentation
    # perturbs (it caused a spurious failure in the Time double-conversion test). SSE has no excess precision and so also
    # matches the ARM target's VFP behaviour, making host float results a better proxy for the firmware's.
    add_compile_options(-m32 -mfpmath=sse -msse2 -Og -ggdb3)
    add_link_options(-m32)
    add_subdirectory(32bit_unit_tests)
endif ()
add_subdirectory(spec)
add_subdirectory(unit)

