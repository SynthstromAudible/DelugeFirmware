configure_file(version.h.in version.h @ONLY)

file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS *.S *.c *.h *.cpp *.hpp)
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
)

target_compile_definitions(deluge PUBLIC
    $<$<CONFIG:DEBUG>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG>:ENABLE_TEXT_OUTPUT=1>
    $<$<CONFIG:RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

if(ENABLE_RTT)
    message(STATUS "RTT enabled for deluge")
    target_compile_definitions(deluge PUBLIC
        $<$<CONFIG:DEBUG>:HAVE_RTT=1>
        $<$<CONFIG:RELWITHDEBINFO>:HAVE_RTT=1>
    )
    target_link_libraries(deluge PUBLIC RTT)
endif(ENABLE_RTT)

if(ENABLE_SYSEX_LOAD)
    message(STATUS "Sysex firmware loading enabled for deluge")
    target_compile_definitions(deluge PUBLIC ENABLE_SYSEX_LOAD=1)
endif(ENABLE_SYSEX_LOAD)
