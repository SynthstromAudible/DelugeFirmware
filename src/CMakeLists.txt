cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

target_sources(deluge PUBLIC main.c resetprg.c c_lib_alternatives.S malloc.c terminate.cpp memmove.c)

add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)
add_subdirectory(RZA1)
add_subdirectory(RTT)
add_subdirectory(fatfs)
add_subdirectory(NE10)
add_subdirectory(lib)
