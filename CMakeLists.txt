cmake_minimum_required(VERSION 3.23)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/CMakeToolchainDeluge.cmake)

project(Deluge 
    LANGUAGES CXX C ASM
    VERSION 1.0.0
)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT error)

if( IPO_SUPPORTED )
    message(STATUS "IPO / LTO enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION 
        $<$<CONFIG:DEBUG>:OFF>
        $<$<CONFIG:RELEASE>:ON>
    )
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

# This function lets us add multiple firmware "configurations"
# that each have their own set of flags from the same source
function(add_firmware EXECUTABLE BASE)
    add_executable(${EXECUTABLE} ${ARGN})

    set_target_properties(${EXECUTABLE} 
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS ON
    )

    target_link_libraries(${EXECUTABLE} ${BASE})

    cmake_path(SET BIN_FILE ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE}.bin)
    cmake_path(SET HEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE}.hex)
    cmake_path(SET MAP_FILE ${CMAKE_CURRENT_BINARY_DIR}/${EXECUTABLE}.map)

    # add link options to generate memory map
    target_link_libraries(${EXECUTABLE} -Wl,\"-Map=${MAP_FILE}\",--cref)

    set(OBJCOPY_ELF_TO_BIN_COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${EXECUTABLE}> ${BIN_FILE})
    set(OBJCOPY_ELF_TO_HEX_COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${EXECUTABLE}> ${HEX_FILE})

    # generate .bin firmware file
    add_custom_command(TARGET ${EXECUTABLE} 
        POST_BUILD
        COMMAND ${OBJCOPY_ELF_TO_BIN_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${BIN_FILE}")

    # generate .hex firmware file
    add_custom_command(TARGET ${EXECUTABLE} 
        POST_BUILD
        COMMAND ${OBJCOPY_ELF_TO_HEX_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${HEX_FILE}")

    #
    set_property(TARGET ${EXECUTABLE}
        PROPERTY 
            ADDITIONAL_CLEAN_FILES ${BIN_FILE} ${MAP_FILE} ${HEX_FILE})

    if(CMAKE_BUILD_TYPE EQUAL "Release")
        message(STATUS "IPO enabled")
        set_property(TARGET ${EXECUTABLE} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        set_property(TARGET RZA1 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    find_program(PRETTYSIZE_CMD prettysize prettysize.py)

    if(NOT PRETTYSIZE_CMD STREQUAL "PRETTYSIZE_CMD-NOTFOUND")
        add_custom_command(
            TARGET ${EXECUTABLE} POST_BUILD
            COMMAND prettysize
            -l ${CMAKE_CURRENT_LIST_DIR}/linker_script_rz_a1l.ld
            -s ${CMAKE_SIZE} $<TARGET_FILE:${EXECUTABLE}>
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Displaying usage graph for ${EXECUTABLE}")
    endif()
endfunction(add_firmware)

message(STATUS "Building for ${CMAKE_BUILD_TYPE}")

# Global compilation options
add_compile_options(

    # Set preinclude
    $<$<COMPILE_LANGUAGE:CXX>:-includepreinclude_cxx.h>

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

    # Warnings
    # $<$<CONFIG:DEBUG>:-Wall> # TODO: Enable when ready
    $<$<CONFIG:DEBUG>:-Wnull-dereference>
    $<$<CONFIG:DEBUG>:-Wno-psabi>

    # Stack usage
    $<$<CONFIG:DEBUG>:-Wstack-usage=100>
    $<$<CONFIG:RELEASE>:-Wstack-usage=1000>
)

add_link_options(
    -T ${PROJECT_SOURCE_DIR}/linker_script_rz_a1l.ld
    LINKER:--gc-sections

    -nostartfiles # Don't emit startfiles
    -estart # Set the entrypoint to 'start' (see RZA1/compiler/asm/start.S)
)

# Add our sources
add_subdirectory(src)

# RTT Debug options
option(ENABLE_RTT "Enable RTT output" ON)

if(ENABLE_RTT)
    message(STATUS "RTT enabled")

    target_compile_definitions(RZA1 PUBLIC
        $<$<CONFIG:DEBUG>:ENABLE_TEXT_OUTPUT=1>
        $<$<CONFIG:DEBUG>:HAVE_RTT=1>
    )
    target_compile_definitions(Deluge INTERFACE
        $<$<CONFIG:DEBUG>:ENABLE_TEXT_OUTPUT=1>
        $<$<CONFIG:DEBUG>:HAVE_RTT=1>
    )
endif()

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
endif()

# Setup our firmware build options
set(DELUGE_SCREEN "BOTH" CACHE STRING "Select the Deluge screen type to build for (or BOTH)")
set(DELUGE_SCREEN_VALID OLED 7SEG BOTH)
set_property(CACHE DELUGE_SCREEN PROPERTY STRINGS ${DELUGE_SCREEN_VALID})

if(NOT ${DELUGE_SCREEN} IN_LIST DELUGE_SCREEN_VALID)
    message(FATAL_ERROR "${DELUGE_SCREEN} is not a proper screen type. Please choose one of 7SEG, OLED, or BOTH.")
endif()

# 7SEG configuration
if((DELUGE_SCREEN STREQUAL "7SEG") OR DELUGE_SCREEN STREQUAL "BOTH")
    message(STATUS "Building for 7-segment model")
    add_firmware(Deluge7seg Deluge)
endif()

# OLED Configuration
if((DELUGE_SCREEN STREQUAL "OLED") OR DELUGE_SCREEN STREQUAL "BOTH")
    add_firmware(DelugeOLED Deluge)
    message(STATUS "Building for OLED model")
    target_compile_definitions(DelugeOLED PUBLIC HAVE_OLED=1)
endif()
