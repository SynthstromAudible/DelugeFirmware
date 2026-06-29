cmake_minimum_required(VERSION 3.24)
project(DelugeTests)

if (WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Gcc")
    # CPPUTEST_PLATFORM defaults to Gcc, which causes the test binaries to
    # segfault on Windows/Clang builds, since they use the stdlib from MSVC.
    set(CPPUTEST_PLATFORM VisualCpp)
endif ()

set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 26)

# Opt-in dynamic analysis for the host test binaries. Enable with -DDELUGE_TEST_SANITIZERS=ON.
#
# AddressSanitizer (heap/stack overflow, use-after-free, double-free) and UndefinedBehaviorSanitizer both work in the
# -m32 host build (the i386 libasan/libubsan runtimes are required - on Arch that's `lib32-gcc-libs`). NOTE: LeakSanitizer
# (ASan's leak-at-exit detector) is x86-64 only and silently does nothing under -m32, which is why host leak checking is
# done explicitly via the TrackingAllocator's outstanding-allocation set (see tests/spec/tracking_allocator_spec.cpp)
# rather than via LSan.
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

# ETL (etl::string) is used by util/string helpers pulled in transitively via functions.h,
# so make it available to every test target (32bit_unit_tests, spec, unit).
include(FetchContent)
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.39.4
)
FetchContent_MakeAvailable(etl)
link_libraries(etl::etl)

#needs to support -m32 since the allocator conformance assumes a 32 bit architecture
if (UNIX AND NOT APPLE)
    # -mfpmath=sse -msse2: do double math in SSE (64-bit IEEE) rather than the x87 stack (80-bit). x87 excess precision
    # makes exact float comparisons depend on when intermediates spill to memory, which sanitizer instrumentation
    # perturbs (it caused a spurious failure in the Time double-conversion test). SSE has no excess precision and so also
    # matches the ARM target's VFP behaviour, making host float results a better proxy for the firmware's.
    add_compile_options(-m32 -mfpmath=sse -msse2 -Og -ggdb3)
    add_link_options(-m32)

    # Build the deluge_rust umbrella staticlib (i686) ONCE and share it across the
    # conformance specs (it re-exports both the deluge_alloc and deluge_resource C
    # ABIs). Defining the build here avoids two subdirs emitting colliding rules for
    # the same output.
    set(RUST_WS ${CMAKE_CURRENT_LIST_DIR}/../crates)
    set(RUST_TARGET i686-unknown-linux-gnu)
    set(RUST_LIB ${RUST_WS}/target/${RUST_TARGET}/release/libdeluge_rust.a)
    file(GLOB_RECURSE RUST_SOURCES
        ${RUST_WS}/deluge_alloc/src/*.rs ${RUST_WS}/deluge_resource/src/*.rs ${RUST_WS}/deluge_rust/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB}
        COMMAND cargo build -p deluge_rust --release --target ${RUST_TARGET}
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES}
            ${RUST_WS}/Cargo.toml ${RUST_WS}/deluge_alloc/Cargo.toml
            ${RUST_WS}/deluge_resource/Cargo.toml ${RUST_WS}/deluge_rust/Cargo.toml
        COMMENT "Building Rust deluge_rust staticlib (${RUST_TARGET})"
        VERBATIM)
    add_custom_target(deluge_rust_rs_build DEPENDS ${RUST_LIB})
    add_library(deluge_rust_rs STATIC IMPORTED GLOBAL)
    set_target_properties(deluge_rust_rs PROPERTIES IMPORTED_LOCATION ${RUST_LIB})
    add_dependencies(deluge_rust_rs deluge_rust_rs_build)

    # Rust core conformance: the umbrella staticlib linked into -m32 CppSpec drivers.
    # (The legacy C++ MemoryRegion + its MemoryManagerTests / memory_specs targets were
    # retired with the allocator; the shared conformance headers live in spec_memory/,
    # and the reusable mocks in 32bit_unit_tests/mocks/ are pulled in directly by spec/.)
    add_subdirectory(spec_alloc)
    add_subdirectory(spec_resource)
endif ()
add_subdirectory(spec)
add_subdirectory(unit)

