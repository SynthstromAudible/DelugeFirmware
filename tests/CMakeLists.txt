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

find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME gfx_layer_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/check_gfx_layer.py
                     --root ${CMAKE_CURRENT_LIST_DIR}/../src/gfx)
    add_test(NAME layer_rules
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/check_layer_rules.py)
endif ()

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

add_compile_options(-Wno-unknown-warning-option)
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
    file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
        ${RUST_WS}/deluge_alloc/src/*.rs ${RUST_WS}/deluge_resource/src/*.rs ${RUST_WS}/deluge_rust/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB}
        COMMAND cargo build -p deluge_rust --release --target ${RUST_TARGET}
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES}
            ${RUST_WS}/Cargo.lock
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

    # A SEPARATE umbrella build, with the `sim` feature on, exclusively for
    # spec_sample_reader: `deluge_sample_reader`'s `deluge_sample_reader_*` /
    # `deluge_sample_read` C-ABI symbols are only emitted (`#[unsafe(no_mangle)]`) under
    # `target_os = "none"` or the `host_app`/`sim` feature gates (see that crate's abi.rs) --
    # plain `deluge_rust_rs` above builds with none of those, so it never exports them. Uses
    # its own `--target-dir` (not the plain `deluge_rust_rs` build's) so the two feature sets
    # never fight over the same output path.
    set(RUST_LIB_SIM ${RUST_WS}/target-tests-sim/${RUST_TARGET}/release/libdeluge_rust.a)
    # Own glob (not layered on RUST_SOURCES -- GLOB_RECURSE patterns are wildcards, not a list of
    # already-resolved paths to re-glob), mirroring sim/CMakeLists.txt's own SIM_RUST_SOURCES.
    file(GLOB_RECURSE RUST_SOURCES_SIM CONFIGURE_DEPENDS
        ${RUST_WS}/deluge_alloc/src/*.rs ${RUST_WS}/deluge_resource/src/*.rs
        ${RUST_WS}/deluge_sample_reader/src/*.rs ${RUST_WS}/deluge_sample_fill/src/*.rs
        ${RUST_WS}/deluge_sample_source/src/*.rs ${RUST_WS}/deluge_rust/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB_SIM}
        COMMAND cargo build -p deluge_rust --release --target ${RUST_TARGET} --features sim
            --target-dir ${RUST_WS}/target-tests-sim
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES_SIM}
            ${RUST_WS}/Cargo.lock
            ${RUST_WS}/Cargo.toml ${RUST_WS}/deluge_alloc/Cargo.toml
            ${RUST_WS}/deluge_resource/Cargo.toml ${RUST_WS}/deluge_rust/Cargo.toml
            ${RUST_WS}/deluge_sample_reader/Cargo.toml ${RUST_WS}/deluge_sample_fill/Cargo.toml
            ${RUST_WS}/deluge_sample_source/Cargo.toml
        COMMENT "Building Rust deluge_rust staticlib with the sim feature (${RUST_TARGET})"
        VERBATIM)
    add_custom_target(deluge_rust_rs_sim_build DEPENDS ${RUST_LIB_SIM})
    add_library(deluge_rust_rs_sim STATIC IMPORTED GLOBAL)
    set_target_properties(deluge_rust_rs_sim PROPERTIES IMPORTED_LOCATION ${RUST_LIB_SIM})
    add_dependencies(deluge_rust_rs_sim deluge_rust_rs_sim_build)

    add_subdirectory(spec_sample_reader)
endif ()

# deluge_gfx_oled / deluge_gfx_grid: pure drawing primitives, shared by spec/ and unit/
# (both are subdirectories of this single tests/ project, so the gfx subdirectory is
# added exactly once here rather than from each of them).
cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/../src)
add_subdirectory(../src/gfx gfx)

# deluge_notify: the model->UI notification channel, shared by spec/ and unit/ on the
# same terms as gfx above (added exactly once here rather than from each of them).
add_subdirectory(../src/notify notify)

add_subdirectory(spec)
add_subdirectory(spec_io)
add_subdirectory(spec_host_passthrough)
add_subdirectory(spec_storage)
add_subdirectory(spec_audio_stream)
add_subdirectory(spec_dsp)
add_subdirectory(spec_sync)
add_subdirectory(unit)

