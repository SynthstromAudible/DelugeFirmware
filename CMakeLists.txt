cmake_minimum_required(VERSION 3.25)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/scripts/cmake/CMakeToolchainDeluge.cmake)

project(DelugeFirmware LANGUAGES CXX C ASM)

# This function lets us add multiple firmware "configurations"
# that each have their own set of flags from the same source
function(add_firmware EXECUTABLE BASE)
    add_executable(${EXECUTABLE} ${ARGN})

    set_target_properties(${EXECUTABLE} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
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
    add_custom_command(
        TARGET ${EXECUTABLE} POST_BUILD
        COMMAND ${OBJCOPY_ELF_TO_BIN_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${BIN_FILE}")

    # generate .hex firmware file
    add_custom_command(
        TARGET ${EXECUTABLE} POST_BUILD
        COMMAND ${OBJCOPY_ELF_TO_HEX_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Objcopying ${EXECUTABLE} to firmware ${HEX_FILE}")

    # 
    set_property(TARGET ${EXECUTABLE}
        PROPERTY ADDITIONAL_CLEAN_FILES ${BIN_FILE} ${MAP_FILE} ${HEX_FILE})

    if (CMAKE_BUILD_TYPE EQUAL "Release")
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

## Global compilation options
add_compile_options(
    # Debug symbols
    $<$<CONFIG:DEBUG>:-g> # Include
    $<$<CONFIG:RELEASE>:-s> # Strip

    # Optimization level
    $<$<CONFIG:DEBUG>:-O0>
    $<$<CONFIG:RELEASE>:-O2>

    # Link time optimizations
    $<$<CONFIG:RELEASE>:-flto>

    # C++ stuff
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>

    # ASM stuff  
    $<$<COMPILE_LANGUAGE:ASM>:-x>
    $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
  

    -Wnull-dereference
    -Wstack-usage=1000
    -Wno-psabi
)


## Colored output
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


## Add our sources
add_subdirectory(src)


## RTT Debug options
option(ENABLE_RTT "Enable RTT output" ON)
if(ENABLE_RTT AND(CMAKE_BUILD_TYPE STREQUAL "Debug"))
    message(STATUS "RTT enabled")
    set(RTT_FLAGS
        ENABLE_TEXT_OUTPUT=1
        HAVE_RTT=1)

    target_compile_definitions(RZA1 PUBLIC ${RTT_FLAGS})
    target_compile_definitions(DelugeFirmware INTERFACE ${RTT_FLAGS})
endif()


## Setup our firmware build options
option(DELUGE_SCREEN "Select the Deluge screen type to build for (or BOTH)" "BOTH")

# 7SEG configuration
if((DELUGE_SCREEN STREQUAL "7SEG") OR(DELUGE_SCREEN STREQUAL "BOTH"))
    message(STATUS "Building for 7-segment model")
    add_firmware(DelugeFirmware7seg DelugeFirmware)
endif()

# OLED Configuration
if((DELUGE_SCREEN STREQUAL "OLED") OR(DELUGE_SCREEN STREQUAL "BOTH"))
    add_firmware(DelugeFirmwareOLED DelugeFirmware)
    message(STATUS "Building for OLED model")
    target_compile_definitions(DelugeFirmwareOLED PUBLIC HAVE_OLED=1)
endif()
