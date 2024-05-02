# Add the sources to the target
file(GLOB_RECURSE OS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.S
        ${CMAKE_CURRENT_LIST_DIR}/*.c
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

target_sources(deluge PUBLIC ${OS_SOURCES})

target_include_directories(deluge PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${SHARED_INCLUDE}
)