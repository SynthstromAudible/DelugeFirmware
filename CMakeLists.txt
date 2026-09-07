cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0135 NEW)

# Default to the GCC toolchain, but let -DCMAKE_TOOLCHAIN_FILE=... win. An
# unconditional set() here shadows the cache variable the command line creates,
# so an alternative toolchain (e.g. the clang prototype) was silently ignored
# and the build fell back to GCC while appearing to succeed.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/CMakeToolchainDeluge.cmake)
endif()

project(Deluge
    LANGUAGES CXX C ASM
    VERSION 1.3.0
)
include(FetchContent)
include(ProcessorCount)
ProcessorCount(HOST_NUM_CORES)

# Create our version string
set(RELEASE_TYPE "dev" CACHE STRING "The type of releasepack to build")
set_property(CACHE RELEASE_TYPE PROPERTY STRINGS dev nightly alpha beta rc release)

if(RELEASE_TYPE STREQUAL "release")
    set(SEMVER_PRERELEASE_ID "")
else()
    set(SEMVER_PRERELEASE_ID "-${RELEASE_TYPE}")
endif()

set(Python_ROOT $ENV{DBT_TOOLCHAIN_ROOT}/python CACHE STRING "Path to the Python installation")
find_package(Python REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)

# Get the result of "git rev-parse --short HEAD"
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the result of "git status --porcelain"
execute_process(
    COMMAND ${GIT_EXECUTABLE} status --porcelain
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Today's date
string(TIMESTAMP DATE_TODAY "%Y.%m.%d")

# Build our version string
if(RELEASE_TYPE STREQUAL "release")
    set(BUILD_VERSION_SUFFIX "-c${PROJECT_VERSION}") # Used for filename
else()
    set(BUILD_VERSION_SUFFIX "-v${PROJECT_VERSION}${SEMVER_PRERELEASE_ID}+${DATE_TODAY}-${GIT_COMMIT_SHORT}") # Used for filename
endif()

string(REPLACE "." "_" BUILD_VERSION_SUFFIX ${BUILD_VERSION_SUFFIX})

option(ENABLE_VERSIONED_OUTPUT "Tag the resulting output files with a semver label" OFF)

if(ENABLE_VERSIONED_OUTPUT)
    set(EXECUTABLE_VERSION_SUFFIX ${BUILD_VERSION_SUFFIX})
else()
    set(EXECUTABLE_VERSION_SUFFIX "")
endif(ENABLE_VERSIONED_OUTPUT)

# Optimization level for the DEBUG config. -Og by default (debuggable). The Rust/Embassy device build
# can raise this to -O2, because it must link DEBUG objects -- Release's slim-LTO objects are
# unreadable to its linker. Whether -Og is actually too slow for real-time audio on hardware is
# UNMEASURED; see the use site for what is and is not known.
set(DELUGE_DEBUG_OPT_LEVEL "-Og" CACHE STRING "Optimization flag for the Debug config (e.g. -Og, -O2)")

# SYSEX Load option
option(ENABLE_SYSEX_LOAD "Enable loading firmware over midi sysex" OFF)

# MATRIX DRIVER pad logging
option(ENABLE_MATRIX_DEBUG "Enable logging of pad events" OFF)

# Colored output
set(CMAKE_COLOR_DIAGNOSTICS ON)
# COMPILE_LANG_AND_ID, not CXX_COMPILER_ID: the latter queries the CXX compiler
# whatever language is being compiled, so with a clang C++ compiler the flag also
# lands on the ASM command line — which fails if ASM is assembled by GCC.
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fansi-escape-codes>)

# ##### Documentation generation #######
# check if Doxygen is installed
find_package(Doxygen
    OPTIONAL_COMPONENTS dot mscgen dia
)

if(DOXYGEN_FOUND)
    FetchContent_Declare(doxygen-awesome-css
        URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.2.1.tar.gz
        URL_HASH MD5=340d3a206794ac01a91791c2a513991f
    )
    FetchContent_MakeAvailable(doxygen-awesome-css)

    if(NOT ${DOXYGEN_HAVE_DOT})
        message(
            "Can't find GraphViz DOT tool for generating images."
            "Make sure it's on your PATH or install GraphViz")
    endif()

    set(DOXYGEN_PROJECT_NAME "Deluge Firmware")
    string(TIMESTAMP BRIEF "Build date: %Y.%m.%d")
    set(DOXYGEN_PROJECT_BRIEF ${BRIEF})
    set(DOXYGEN_PROJECT_LOGO assets/DelugeLogo.png)
    set(DOXYGEN_HTML_FOOTER assets/DoxygenFooter.html)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_EXAMPLE_RECURSIVE YES)

    set(DOXYGEN_NUM_PROC_THREADS ${HOST_NUM_CORES})

    # From doxygen-awesome
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_DISABLE_INDEX NO)
    set(DOXYGEN_FULL_SIDEBAR NO)
    set(DOXYGEN_HTML_COLORSTYLE LIGHT)
    set(DOXYGEN_HTML_EXTRA_STYLESHEET "${doxygen-awesome-css_SOURCE_DIR}/doxygen-awesome.css")

    set(DOXYGEN_CREATE_SUBDIRS YES)
    set(DOXYGEN_CREATE_SUBDIRS_LEVEL 1) # 32 subdirectories w/~200 files each. 
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_INTERNAL_DOCS NO)
    set(DOXYGEN_SOURCE_BROWSER NO)
    set(DOXYGEN_QUIET YES)
    set(DOXYGEN_WARNINGS NO)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_WARN_IF_DOC_ERROR NO)
    set(DOXYGEN_WARN_IF_INCOMPLETE_DOC NO)
    set(DOXYGEN_DOT_GRAPH_MAX_NODES 100)
    set(DOXYGEN_GRAPHICAL_HIERARCHY NO)
    set(DOXYGEN_DIRECTORY_GRAPH NO)
    set(DOXYGEN_DOT_CLEANUP NO)

    # README.md files in source directories are
    # revealed in their directory detail page instead
    # of in 'Related Pages'.  Added in doxygen version 1.13. 
    set(DOXYGEN_IMPLICIT_DIR_DOCS YES)

    # set(DOXYGEN_CLANG_ASSISTED_PARSING YES)
    # SET(DOXYGEN_CLANG_DATABASE_PATH build)

    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE ./README.md)

    file(GLOB markdown_SOURCES *.md)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/html/)

    # 1) Removed 'docs' directory to exclude files 
    # with relative links to GitHub repository
    # leaving doxygen to generate just the API documentation.
    # 2) removed build/src/deluge so completion does not depends on build release
    # Doxygen fails with error after a 'dbt clean' when build/src/deluge is included
    doxygen_add_docs(doxygen ${markdown_SOURCES} src/deluge)
else(DOXYGEN_FOUND)
    message(WARNING
        "Doxygen needs to be installed to generate documentation."
        "Please install from https://github.com/doxygen/doxygen/releases")
endif(DOXYGEN_FOUND)

# Global compilation options
add_compile_options(

    # Feature flags
    -fdiagnostics-parseable-fixits
    -fsigned-char
    -D_GNU_SOURCE # for strcasestr

    # Debug symbols, also for release so we get a full ELF
    $<$<CONFIG:DEBUG,RELEASE,RELWITHDEBINFO>:-ggdb3> # Include

    # Optimization level.
    #
    # DELUGE_DEBUG_OPT_LEVEL exists for the Rust/Embassy device build. build.rs links the C++ app from
    # pre-built object files, and it cannot read Release/RelWithDebInfo's slim-LTO objects, so the device
    # firmware is forced to consume DEBUG objects. Set this to -O2 to get an optimized, non-LTO app the
    # Rust link can still consume:
    #
    #   cmake -B build-gcc -DDELUGE_DEBUG_OPT_LEVEL=-O2 && ./dbt rust --release
    #
    # The default stays -Og so ordinary Debug builds remain debuggable.
    #
    # This comment used to assert that -Og (plus the unconditional -fno-inline-functions below) is "far
    # too slow for real-time audio on the RZ/A1L", citing 15-19 ms routine latencies and continuous voice
    # culling. That claim is RETRACTED as unsupported -- not disproven, but not established either, and it
    # should not be repeated until someone measures it. Both cited symptoms come from wall-clock
    # instruments that a halted core corrupts, and every observation behind them was taken with a debug
    # probe attached:
    #
    #   - The latency figure is `current_time - last_call_time` in deluge_app_render
    #     (audio_engine.cpp) -- ENTRY-TO-ENTRY period, not compute time. Anything that stops the core
    #     between two renders inflates it while the code runs exactly as fast as before.
    #   - Culling keys off `dspTime = getAverageRunTimeForTask(routine_task_id) * 44100`
    #     (AudioEngine::setDireness), the measured elapsed run time of the render task. A probe halt
    #     landing inside a render inflates that too.
    #
    # So the two are not independent confirmations; they are one apparatus reported twice, and neither
    # separates "the code got slower" from "the core stopped". probe-rs is independently suspected of
    # stalling this target (it is why tools/deluge_jlink.py exists), and deluge_app_render's own warning
    # already had to be rate-limited once because unthrottled logging turned the latency figure into a
    # measurement of the logging.
    #
    # To settle it: flash ONE -Og image and run it under tools/deluge_run.py (probe-rs) and then
    # tools/deluge_jlink.py (J-Link), same SD card, comparing the "Audio routine latency high" lines and
    # cull counts. If J-Link reports nominal figures, the slowness was the probe and -Og is usable.
    $<$<CONFIG:DEBUG>:${DELUGE_DEBUG_OPT_LEVEL}>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-O2>
        $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-fsplit-loops>
        $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-funswitch-loops>

        # does not block inlining, just blocks considering all functions for inlining
    # https://developer.arm.com/documentation/100067/0600/Compiler-Command-line-Options/-finline-functions---fno-inline-functions?lang=en
    #
    # Applies to clang too, despite meaning something stronger there: GCC disables
    # only its -finline-functions pass (verified at -O2: -finline-small-functions,
    # -findirect-inlining, -finline-functions-called-once, -fearly-inlining and
    # -fpartial-inlining all stay enabled), whereas clang stamps the `noinline`
    # ATTRIBUTE on every definition. Gating it off for clang was measured and is
    # worse on every axis — see the note in src/bsp/rust/.cargo/config.toml.
    -fno-inline-functions

    # Link time optimizations
    #
    # Split by compiler. GCC's slim-LTO objects are what the Rust device link
    # cannot consume (see DELUGE_DEBUG_OPT_LEVEL above), which is why that path
    # is pinned to Debug objects. Clang emits ThinLTO bitcode, which lld reads
    # directly — so the clang build can use real LTO in Release. -flto=auto,
    # -fuse-linker-plugin, -flto-partition and -fdevirtualize-at-ltrans are all
    # GCC spellings clang rejects.
    $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:GNU>>:-flto=auto>
#        $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:GNU>>:-save-temps=obj>

        $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:GNU>>:-fuse-linker-plugin>
    # this makes the optimization better at the cost of linking being REALLY slow
        $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:GNU>>:-flto-partition=none>

        $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:GNU>>:-fdevirtualize-at-ltrans>

    # COMPILE_LANGUAGE guard: CXX_COMPILER_ID is true whatever language is being
    # compiled, and assembly goes through arm-none-eabi-gcc, which rejects
    # -flto=thin.
        $<$<AND:$<CONFIG:RELEASE,RELWITHDEBINFO>,$<CXX_COMPILER_ID:Clang>,$<COMPILE_LANGUAGE:C,CXX>>:-flto=thin>
    -ffunction-sections
    -fdata-sections
    -DUSE_TASK_MANAGER

    # C++ stuff
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>

    # ASM stuff
    $<$<COMPILE_LANGUAGE:ASM>:-x>
    $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
)

# Add libraries
add_subdirectory(lib)

option(MAINTAINER_MODE "Enables all warnings and errors" OFF)

# Project-only compile options (warnings, etc.)
set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 26)
list(APPEND DELUGE_COMMON_COMPILE_OPTIONS
    $<$<AND:$<BOOL:${MAINTAINER_MODE}>,$<CXX_COMPILER_ID:GNU>>:-fanalyzer> # static analysis

    # Warnings
    -Warray-bounds=1
    -Wnull-dereference
    -Wparentheses
    -Wsequence-point
    -Werror=switch
    $<$<BOOL:${MAINTAINER_MODE}>:-Wextra>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wall>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wabi>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wpedantic>
    $<$<CONFIG:DEBUG>:-Wno-psabi>
    $<$<CONFIG:DEBUG>:-Wno-unused-parameter>

    $<$<CONFIG:DEBUG>:-Werror=write-strings>

    # Offsetof for non standard types is supported in GCC,
    # if another compiler does not support it they are obligated to error

    # Stack usage
    # tracked for diagnostic usage. We have a 32k stack so it's not that important
    # primarily concerned with unbounded stack use
    $<$<CONFIG:DEBUG>:-Wstack-usage=2000>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-Wstack-usage=2000>
)

# Freestanding UndefinedBehaviorSanitizer layer (see src/deluge/io/debug/ubsan_handlers.cpp). Off by default; enable
# with -DDELUGE_UBSAN=ON. We compile with -fsanitize=undefined but do NOT add it to link options - there is no libubsan
# on bare metal, the __ubsan_handle_* hooks are provided in-tree. `alignment` is excluded because the custom allocator
# performs intentional unaligned boundary-tag reads; the vptr check is already off because RTTI is disabled.
#
# Instrumenting the *whole* firmware roughly doubles .text and overflows the internal-RAM region, so UBSAN is scoped to
# a configurable set of source subdirectories (the corruption-prone allocator and container code by default). Point
# DELUGE_UBSAN_SOURCE_DIRS at whichever modules you are hunting in.
option(DELUGE_UBSAN "Enable the freestanding UndefinedBehaviorSanitizer layer" OFF)
set(DELUGE_UBSAN_SOURCE_DIRS "memory;util/container"
    CACHE STRING "Semicolon-separated subdirs under src/deluge to instrument when DELUGE_UBSAN is ON")

# GCC static analyzer (-fanalyzer). Off by default; enable with -DDELUGE_ANALYZER=ON. This is a compile-time (not
# runtime) pass: it walks each function's paths looking for leaks, use-after-free, double-free, mismatched alloc/dealloc
# and similar. Thanks to the [[gnu::malloc(delugeDealloc, 1)]] annotation on delugeAlloc it follows ownership through the
# Deluge allocator, not just libc malloc/free. It does NOT change codegen, so the resulting binary is unaffected.
#
# -fanalyzer is slow and, on C++, prone to false positives, so it is scoped to a configurable set of subdirs (same idea
# as DELUGE_UBSAN) and is NOT wired into -Werror. Read its warnings as leads to investigate, not hard failures. Point
# DELUGE_ANALYZER_SOURCE_DIRS at whichever modules you are auditing.
option(DELUGE_ANALYZER "Run GCC's -fanalyzer over selected source dirs" OFF)
set(DELUGE_ANALYZER_SOURCE_DIRS "memory;util/container"
    CACHE STRING "Semicolon-separated subdirs under src/deluge to analyze when DELUGE_ANALYZER is ON")

# Deluge sources
add_subdirectory(src)

# DELUGE_UBSAN / DELUGE_ANALYZER apply their target-scoped properties here, after add_subdirectory(src) has
# created deluge_app (src/deluge/CMakeLists.txt) -- target_compile_definitions() and set_source_files_properties()
# TARGET_DIRECTORY both require the target to already exist.
if(DELUGE_UBSAN)
    target_compile_definitions(deluge_app PUBLIC DELUGE_UBSAN=1)
    foreach(_ubsan_dir ${DELUGE_UBSAN_SOURCE_DIRS})
        file(GLOB_RECURSE _ubsan_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/deluge/${_ubsan_dir}/*.cpp)
        if(_ubsan_srcs)
            set_source_files_properties(${_ubsan_srcs} TARGET_DIRECTORY deluge_app
                PROPERTIES COMPILE_OPTIONS "-fsanitize=undefined;-fno-sanitize=alignment")
        endif()
    endforeach()
endif()

if(DELUGE_ANALYZER)
    foreach(_an_dir ${DELUGE_ANALYZER_SOURCE_DIRS})
        file(GLOB_RECURSE _an_srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/deluge/${_an_dir}/*.cpp)
        if(_an_srcs)
            set_source_files_properties(${_an_srcs} TARGET_DIRECTORY deluge_app
                PROPERTIES COMPILE_OPTIONS "-fanalyzer")
        endif()
    endforeach()
endif()
