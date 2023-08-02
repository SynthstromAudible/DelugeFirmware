cmake_minimum_required(VERSION 3.23)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/CMakeToolchainDeluge.cmake)

project(Deluge
    LANGUAGES CXX C ASM
    VERSION 1.0.0
)

# Check for IPO support
include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT error)

if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION
        $<$<CONFIG:DEBUG>:OFF>
        $<$<CONFIG:RELEASE>:ON>
    )
endif()

# Create our version string
set(RELEASE_TYPE "dev" CACHE STRING "The type of releasepack to build")
set_property(CACHE RELEASE_TYPE PROPERTY STRINGS dev nightly alpha beta rc release)

if(RELEASE_TYPE STREQUAL "release")
    set(SEMVER_PRERELEASE_ID "")
else()
    set(SEMVER_PRERELEASE_ID "-${RELEASE_TYPE}")
endif()

find_package(Git)

if(GIT_FOUND)
    message(VERBOSE "git found: ${GIT_EXECUTABLE} version ${GIT_VERSION_STRING}")
else()
    message(FATAL_ERROR "Git not found. Check your path.")
endif(GIT_FOUND)

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
set(BUILD_VERSION_STRING "${PROJECT_VERSION}${SEMVER_PRERELEASE_ID}-${GIT_COMMIT_SHORT}") # Used in firmware
set(BUILD_VERSION_SUFFIX "-v${PROJECT_VERSION}${SEMVER_PRERELEASE_ID}+${DATE_TODAY}-${GIT_COMMIT_SHORT}") # Used for filename

if(NOT "${GIT_STATUS}" STREQUAL "")
    string(APPEND BUILD_VERSION_SUFFIX "-dirty")
    string(APPEND BUILD_VERSION_STRING "-dirty")
endif()

message(VERBOSE "semver: ${BUILD_VERSION_SUFFIX}")

option(ENABLE_VERSIONED_OUTPUT "Tag the resulting output files with a semver label" OFF)

if(ENABLE_VERSIONED_OUTPUT)
    set(EXECUTABLE_VERSION_SUFFIX ${BUILD_VERSION_SUFFIX})
else()
    set(EXECUTABLE_VERSION_SUFFIX "")
endif(ENABLE_VERSIONED_OUTPUT)

# This function lets us add multiple firmware "configurations"
# that each have their own set of flags from the same source
# It is very much a hack and should be retired as soon as possible
function(add_firmware EXECUTABLE BASE)
    get_target_property(BASE_SOURCES ${BASE} INTERFACE_SOURCES)
    get_target_property(BASE_INCLUDES ${BASE} INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(BASE_LINKS ${BASE} INTERFACE_LINK_LIBRARIES)
    get_target_property(BASE_DEFINITIONS ${BASE} INTERFACE_COMPILE_DEFINITIONS)
    get_target_property(BASE_OPTIONS ${BASE} INTERFACE_COMPILE_OPTIONS)

    add_executable(${EXECUTABLE})
    target_sources(${EXECUTABLE} PRIVATE ${BASE_SOURCES})
    target_include_directories(${EXECUTABLE} PRIVATE ${BASE_INCLUDES})
    target_link_libraries(${EXECUTABLE} ${BASE_LINKS})

    set_property(
        TARGET ${EXECUTABLE}
        APPEND PROPERTY
            COMPILE_DEFINITIONS ${BASE_DEFINITIONS}
    )

    set_property(
        TARGET ${EXECUTABLE}
        APPEND PROPERTY
            COMPILE_OPTIONS ${BASE_OPTIONS}
    )

    set_target_properties(${EXECUTABLE}
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS ON
            SUFFIX "${EXECUTABLE_VERSION_SUFFIX}.elf"
    )

    set(BIN_FILE $<CONFIG>/${EXECUTABLE}${EXECUTABLE_VERSION_SUFFIX}.bin)
    set(HEX_FILE $<CONFIG>/${EXECUTABLE}${EXECUTABLE_VERSION_SUFFIX}.hex)
    set(MAP_FILE $<CONFIG>/${EXECUTABLE}${EXECUTABLE_VERSION_SUFFIX}.map)

    # add link options to generate memory map
    target_link_options(${EXECUTABLE} PUBLIC
        LINKER:-Map=${MAP_FILE}
        LINKER:--cref
    )

    # generate .bin firmware file
    add_custom_command(TARGET ${EXECUTABLE}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${EXECUTABLE}> ${BIN_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${BIN_FILE}"
        DEPENDS ${EXECUTABLE}
        BYPRODUCTS ${BIN_FILE})

    # generate .hex firmware file
    add_custom_command(TARGET ${EXECUTABLE}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -S $<TARGET_FILE:${EXECUTABLE}> ${HEX_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${HEX_FILE}"
        DEPENDS ${EXECUTABLE}
        BYPRODUCTS ${HEX_FILE})

    # Clean up artefacts
    set_property(TARGET ${EXECUTABLE}
        PROPERTY
            ADDITIONAL_CLEAN_FILES ${BIN_FILE} ${MAP_FILE} ${HEX_FILE})

    # find_program(PRETTYSIZE_CMD prettysize prettysize.py)

    # if(NOT PRETTYSIZE_CMD STREQUAL "PRETTYSIZE_CMD-NOTFOUND")
    # add_custom_command(
    # TARGET ${EXECUTABLE} POST_BUILD
    # COMMAND prettysize
    # -l ${CMAKE_CURRENT_LIST_DIR}/linker_script_rz_a1l.ld
    # -s ${CMAKE_SIZE} $<TARGET_FILE:${EXECUTABLE}>
    # WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    # COMMENT "Displaying usage graph for ${EXECUTABLE}")
    # endif()
endfunction(add_firmware)

# Global compilation options
add_compile_options(

    # Feature flags
    -fdiagnostics-parseable-fixits
    -fsigned-char

    # Debug symbols
    $<$<CONFIG:DEBUG>:-g> # Include
    $<$<CONFIG:DEBUG>:-ggdb> # Include
    $<$<CONFIG:RELEASE>:-s> # Strip

    # Optimization level
    $<$<CONFIG:DEBUG>:-Og>
    $<$<CONFIG:RELEASE>:-O2>

    # Link time optimizations
    $<$<CONFIG:RELEASE>:-flto>
    -ffunction-sections
    -fdata-sections

    # C++ stuff
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>

    # ASM stuff
    $<$<COMPILE_LANGUAGE:ASM>:-x>
    $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
)

add_link_options(
    -T ${PROJECT_SOURCE_DIR}/linker_script_rz_a1l.ld
    LINKER:--gc-sections

    -nostartfiles # Don't emit startfiles
    -estart # Set the entrypoint to 'start' (see RZA1/compiler/asm/start.S)
)

# RTT Debug options
option(ENABLE_RTT "Enable RTT output" ON)

# Colored output
option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." ON)

if(FORCE_COLORED_OUTPUT)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Forcing colors for GCC")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Forcing colors for Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif(FORCE_COLORED_OUTPUT)

# Add libraries
add_subdirectory(lib)

# Add our sources
add_subdirectory(src)

# Project-only compile options (warnings, etc.)
target_compile_options(deluge INTERFACE

    # Warnings
    # $<$<CONFIG:DEBUG>:-Wextra> # TODO: Enable when ready
    # $<$<CONFIG:DEBUG>:-Wall> # TODO: Enable when ready
    # $<$<CONFIG:DEBUG>:-Wpedantic> # TODO: Enable when ready
    $<$<CONFIG:DEBUG>:-Wnull-dereference>
    $<$<CONFIG:DEBUG>:-Wno-psabi>

    $<$<CONFIG:DEBUG>:-Werror=write-strings>

    # Stack usage
    $<$<CONFIG:DEBUG>:-Wstack-usage=100>
    $<$<CONFIG:RELEASE>:-Wstack-usage=1000>
)

# 7SEG configuration
add_firmware(deluge7SEG deluge)

# OLED Configuration
add_firmware(delugeOLED deluge)
target_compile_definitions(delugeOLED PUBLIC HAVE_OLED=1)
