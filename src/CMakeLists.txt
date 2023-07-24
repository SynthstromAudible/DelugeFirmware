include_directories(${CMAKE_CURRENT_LIST_DIR})

# Normally this would be add_executable, but since we're using add_firmware,
# we use an interface library to simply collect all the source files we care
# about before creating the real build targets
#
# Once the unified display system/one-bin-file build is done, this can be
# changed to add_executable
add_library(deluge INTERFACE)
target_sources(deluge INTERFACE main.c resetprg.c)

add_subdirectory(deluge)
target_include_directories(deluge INTERFACE 
    ${CMAKE_CURRENT_LIST_DIR} 
    deluge
)

target_link_libraries(deluge INTERFACE
    fatfs
    RTT
    NE10
)

add_library(RZA1 STATIC)
add_subdirectory(RZA1)
target_include_directories(RZA1 PUBLIC 
    deluge
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(RZA1 PRIVATE fatfs RTT)


get_target_property(RZA1_SOURCES RZA1 SOURCES)
add_library(RZA1_OLED STATIC ${RZA1_SOURCES})
target_include_directories(RZA1_OLED PUBLIC 
    deluge
    ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(RZA1_OLED PRIVATE fatfs RTT)
target_compile_definitions(RZA1_OLED PUBLIC HAVE_OLED=1)

# External but in-source libraries
add_library(fatfs STATIC)
target_include_directories(fatfs PUBLIC fatfs)
add_subdirectory(fatfs)

add_library(NE10 STATIC)
add_subdirectory(NE10)
target_include_directories(NE10 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    NE10/common
    NE10/inc
    NE10/modules
)

add_library(RTT STATIC)
target_include_directories(RTT PUBLIC RTT)
add_subdirectory(RTT)
