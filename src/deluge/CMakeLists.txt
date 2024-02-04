configure_file(version.h.in version.h @ONLY)
add_subdirectory(dsp)

# Add the sources to the target
file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.S
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

# Filter out DSP sources as those are built as part of the deluge_dsp library
list(FILTER deluge_SOURCES EXCLUDE REGEX .*/dsp/.*)

target_sources(deluge PUBLIC ${deluge_SOURCES})

target_include_directories(deluge PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${SHARED_INCLUDE}
)

target_link_libraries(deluge PUBLIC
    fatfs
    NE10
    RZA1
    eyalroz_printf
    deluge_dsp
)

target_compile_definitions(deluge PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

if(ENABLE_RTT)
    message(STATUS "RTT enabled for deluge")
    target_compile_definitions(deluge PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:HAVE_RTT=1>
    )
    target_link_libraries(deluge PUBLIC 
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:RTT>
    )
endif(ENABLE_RTT)

if(ENABLE_SYSEX_LOAD)
    message(STATUS "Sysex firmware loading enabled for deluge")
    target_compile_definitions(deluge PUBLIC ENABLE_SYSEX_LOAD=1)
endif(ENABLE_SYSEX_LOAD)
