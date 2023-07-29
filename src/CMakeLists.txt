cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(deluge)
target_sources(deluge INTERFACE main.c resetprg.c)

add_subdirectory(RZA1)
add_subdirectory(RTT)
add_subdirectory(fatfs)
add_subdirectory(NE10)

# 7SEG configuration
add_firmware(deluge7SEG deluge)
target_link_libraries(deluge7SEG RZA1)

# OLED Configuration
add_firmware(delugeOLED deluge)
target_compile_definitions(delugeOLED PUBLIC HAVE_OLED=1)
target_link_libraries(delugeOLED RZA1_OLED)
