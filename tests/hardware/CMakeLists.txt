cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(DelugeHardwareTests C)
    enable_testing()
endif()

# Compile the production driver on the host, substituting hardware declarations.
get_filename_component(OLED_DRIVER "${CMAKE_CURRENT_SOURCE_DIR}/../../src/RZA1/oled/oled_low_level.c" ABSOLUTE)
set(OLED_STUBS "${CMAKE_CURRENT_BINARY_DIR}/oled_cv_stubs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OLED_DRIVER}")
get_filename_component(UART_DRIVER "${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge/drivers/uart/uart.c" ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UART_DRIVER}")
file(STRINGS "${OLED_DRIVER}" OLED_INCLUDES REGEX "^#include \"")
file(STRINGS "${UART_DRIVER}" UART_INCLUDES REGEX "^#include \"")
list(APPEND OLED_INCLUDES ${UART_INCLUDES})
foreach(INCLUDE_LINE IN LISTS OLED_INCLUDES)
    string(REGEX REPLACE "^#include \"([^\"]+)\".*" "\\1" HEADER "${INCLUDE_LINE}")
    # Keep the UART API and ring-buffer helpers real when independent fixes are combined.
    if(HEADER MATCHES "^drivers/uart/")
        continue()
    endif()
    get_filename_component(HEADER_DIR "${OLED_STUBS}/${HEADER}" DIRECTORY)
    file(MAKE_DIRECTORY "${HEADER_DIR}")
    file(WRITE "${OLED_STUBS}/${HEADER}" "/* Hardware declarations are supplied by mock_hardware.h. */\n")
endforeach()

add_executable(oled_cv_driver_tests oled_cv/driver_tests.c)
# Check that the mocks are self-contained, and give clang-tidy an explicit C
# compilation command when the PR annotation action checks the header itself.
add_library(oled_cv_mock_header_check OBJECT oled_cv/mock_hardware.h)
set_source_files_properties(oled_cv/mock_hardware.h PROPERTIES LANGUAGE C)
foreach(TARGET oled_cv_driver_tests oled_cv_mock_header_check)
    target_include_directories(${TARGET} PRIVATE "${OLED_STUBS}" "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src/deluge")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET} PRIVATE -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
    endif()
endforeach()
add_test(NAME OledCVDriverTests COMMAND oled_cv_driver_tests)
