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
    add_compile_options(-m32 -Og -ggdb3)
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

