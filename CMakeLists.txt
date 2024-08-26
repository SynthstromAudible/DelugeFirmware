cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/CMakeToolchainDeluge.cmake)

project(Deluge
    LANGUAGES CXX C ASM
    VERSION 1.3.0
)
include(FetchContent)
include(ProcessorCount)
ProcessorCount(HOST_NUM_CORES)

# Create our version string
set(RELEASE_TYPE "release" CACHE STRING "The type of releasepack to build")
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

# RTT option
option(ENABLE_RTT "Enable RTT output" ON)

# SYSEX Load option
option(ENABLE_SYSEX_LOAD "Enable loading firmware over midi sysex" OFF)

# MATRIX DRIVER pad logging
option(ENABLE_MATRIX_DEBUG "Enable logging of pad events" OFF)

# Colored output
set(CMAKE_COLOR_DIAGNOSTICS ON)
add_compile_options($<$<CXX_COMPILER_ID:Clang>:-fansi-escape-codes>)

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
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_INTERNAL_DOCS YES)
    set(DOXYGEN_SOURCE_BROWSER YES)

    # set(DOXYGEN_CLANG_ASSISTED_PARSING YES)
    # SET(DOXYGEN_CLANG_DATABASE_PATH build)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE README.md)

    file(GLOB markdown_SOURCES *.md)

    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/html/)

    doxygen_add_docs(doxygen ${markdown_SOURCES} docs src/deluge build/src/deluge)
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

    # Optimization level
    $<$<CONFIG:DEBUG>:-Og>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-O2>

    # does not block inlining, just blocks considering all functions for inlining
    # https://developer.arm.com/documentation/100067/0600/Compiler-Command-line-Options/-finline-functions---fno-inline-functions?lang=en
    -fno-inline-functions

    # Link time optimizations
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-flto=auto>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-fuse-linker-plugin>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-fdevirtualize-at-ltrans>
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

# Add the main executable target
add_executable(deluge)

# Link external libraries
target_link_libraries(deluge PUBLIC
        argon
        etl::etl
)

option(MAINTAINER_MODE "Enables all warnings and errors" OFF)

# Project-only compile options (warnings, etc.)
set(DELUGE_C_STANDARD 23)
set(DELUGE_CXX_STANDARD 23)
list(APPEND DELUGE_COMMON_COMPILE_OPTIONS
    $<$<AND:$<BOOL:${MAINTAINER_MODE}>,$<CXX_COMPILER_ID:GNU>>:-fanalyzer> # static analysis

    # Warnings
    $<$<BOOL:${MAINTAINER_MODE}>:-Wextra>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wall>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wabi>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wpedantic>
    $<$<CONFIG:DEBUG>:-Wnull-dereference>
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

target_compile_options(deluge PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})

set_target_properties(deluge
    PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        SUFFIX "${EXECUTABLE_VERSION_SUFFIX}.elf"
)

set(BIN_FILE $<CONFIG>/deluge${EXECUTABLE_VERSION_SUFFIX}.bin)
set(HEX_FILE $<CONFIG>/deluge${EXECUTABLE_VERSION_SUFFIX}.hex)
set(MAP_FILE $<CONFIG>/deluge${EXECUTABLE_VERSION_SUFFIX}.map)

# set(OBJDUMP_FILE $<CONFIG>/deluge${EXECUTABLE_VERSION_SUFFIX}.objdump)
set(NMDUMP_FILE $<CONFIG>/deluge${EXECUTABLE_VERSION_SUFFIX}.nmdump)

# add link options to generate memory map
target_link_options(deluge PUBLIC

    -T ${PROJECT_SOURCE_DIR}/linker_script_rz_a1l.ld
    LINKER:-Map=${MAP_FILE}
    LINKER:--cref
    LINKER:--gc-sections
    LINKER:--entry=start # Set the entrypoint to 'start' (see RZA1/compiler/asm/start.S)

    # Enabled to generate a full ELF, objcopy will do the stripping
    # $<$<CONFIG:RELEASE>:LINKER:--strip-all> # Strip
    -nostartfiles # Don't emit startfiles

    # Print details
    LINKER:--warn-common
    LINKER:--stats

    # $<$<CXX_COMPILER_ID:GNU>:LINKER:--print-memory-usage>

    # Link-Time Optimization
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-flto=auto>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-fuse-linker-plugin>

    # Coloring
    $<$<CXX_COMPILER_ID:GNU>:-fdiagnostics-color=always>
)

# generate .bin firmware file
add_custom_command(TARGET deluge
    POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:deluge> ${BIN_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying deluge to firmware ${BIN_FILE}"
        BYPRODUCTS ${BIN_FILE})

# generate .hex firmware file
add_custom_command(TARGET deluge
    POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -S $<TARGET_FILE:deluge> ${HEX_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying deluge to firmware ${HEX_FILE}"
        BYPRODUCTS ${HEX_FILE})

# # generate .objdump file
# add_custom_command(TARGET deluge
# POST_BUILD
# COMMAND ${CMAKE_OBJDUMP} -t $<TARGET_FILE:deluge> > ${OBJDUMP_FILE}
# WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
# COMMENT "Objdumping deluge firmware ${OBJDUMP_FILE}"
# DEPENDS deluge
# BYPRODUCTS ${OBJDUMP_FILE})

# generate .nmdump file
add_custom_command(TARGET deluge
    POST_BUILD
        COMMAND ${CMAKE_NM} -t d -S -C -r --special-syms --size-sort $<TARGET_FILE:deluge> > ${NMDUMP_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "NM dumping deluge firmware ${NMDUMP_FILE}"
        BYPRODUCTS ${NMDUMP_FILE}
)

# Print size
add_custom_command(TARGET deluge
    POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:deluge>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Clean up artefacts
set_target_properties(deluge
    PROPERTIES
        ADDITIONAL_CLEAN_FILES ${BIN_FILE} ${MAP_FILE} ${HEX_FILE}
)

# Deluge sources
add_subdirectory(src)

# find_program(PRETTYSIZE_CMD prettysize prettysize.py)

# if(NOT PRETTYSIZE_CMD STREQUAL "PRETTYSIZE_CMD-NOTFOUND")
# add_custom_command(
# TARGET deluge POST_BUILD
# COMMAND prettysize
# -l ${CMAKE_CURRENT_LIST_DIR}/linker_script_rz_a1l.ld
# -s ${CMAKE_SIZE} $<TARGET_FILE:deluge>
# WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
# COMMENT "Displaying usage graph for deluge")
# endif()
