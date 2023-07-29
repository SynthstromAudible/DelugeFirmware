cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(deluge)
target_sources(deluge INTERFACE main.c resetprg.c)

add_subdirectory(RZA1)
add_subdirectory(RTT)
add_subdirectory(fatfs)
add_subdirectory(NE10)

