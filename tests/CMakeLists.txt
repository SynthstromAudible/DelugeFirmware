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
    # Rust TLSF core conformance: i686 staticlib linked into a -m32 CppSpec driver.
    # (The legacy C++ MemoryRegion + its MemoryManagerTests / memory_specs targets were
    # retired with the allocator; the shared conformance headers live in spec_memory/,
    # and the reusable mocks in 32bit_unit_tests/mocks/ are pulled in directly by spec/.)
    add_subdirectory(spec_alloc)
endif ()
add_subdirectory(spec)
add_subdirectory(unit)

