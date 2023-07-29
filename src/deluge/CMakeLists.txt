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
    ${CMAKE_CURRENT_LIST_DIR} 
    ${SHARED_INCLUDE}
)

target_link_libraries(deluge INTERFACE
    fatfs
    RTT
    NE10
)
