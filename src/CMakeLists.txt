cmake_path(SET SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR})

target_sources(deluge PUBLIC main.c resetprg.c c_lib_alternatives.S malloc.c terminate.cpp memmove.c)

add_subdirectory(foundation) # bottom of the stack; the layers below link it
add_subdirectory(midi)       # deluge_midi: pure MIDI protocol library (app + tests)
add_subdirectory(OSLikeStuff)
add_subdirectory(deluge)     # deluge_app: the portable application (platform-agnostic)
add_subdirectory(bsp)
add_subdirectory(RZA1)
add_subdirectory(RTT)
add_subdirectory(fatfs)
add_subdirectory(NE10)
add_subdirectory(lib)

# ---------------------------------------------------------------------------
# Firmware executable assembly (RZ/A1L): the portable app library + the HAL.
# (bsp_rza1 + the legacy drivers are linked/compiled by src/bsp/rza1; the arm
# startup objects are the target_sources above.) The host-sim assembles the same
# deluge_app against its own BSP in sim/CMakeLists.txt.
# ---------------------------------------------------------------------------
target_link_libraries(deluge PUBLIC deluge_app RZA1)

if(ENABLE_RTT)
    message(STATUS "RTT enabled for deluge")
    target_compile_definitions(deluge_app PUBLIC $<$<CONFIG:DEBUG,RELWITHDEBINFO>:HAVE_RTT=1>)
    target_link_libraries(deluge PUBLIC $<$<CONFIG:DEBUG,RELWITHDEBINFO>:RTT>)
endif(ENABLE_RTT)
