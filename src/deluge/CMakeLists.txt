configure_file(version.h.in version.h @ONLY)

# Normally this would be add_executable, but since we're using add_firmware,
# we use an interface library to simply collect all the source files we care
# about before creating the real build targets
#
# Once the unified display system/one-bin-file build is done, this can be
# changed to add_executable

add_library(deluge INTERFACE)
file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS *.S *.c *.h *.cpp *.hpp)
target_sources(deluge INTERFACE ${deluge_SOURCES})

target_include_directories(deluge INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${SHARED_INCLUDE}
)

target_link_libraries(deluge INTERFACE
    fatfs
    RTT
    NE10
    fmt::fmt # fmtlib
)

target_compile_definitions(deluge INTERFACE
    $<$<CONFIG:DEBUG>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG>:ENABLE_TEXT_OUTPUT=1>
    $<$<CONFIG:RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

if(ENABLE_RTT)
    message(STATUS "RTT enabled for deluge")
    target_compile_definitions(deluge INTERFACE
        $<$<CONFIG:DEBUG>:HAVE_RTT=1>
        $<$<CONFIG:RELWITHDEBINFO>:HAVE_RTT=1>
    )
endif(ENABLE_RTT)
