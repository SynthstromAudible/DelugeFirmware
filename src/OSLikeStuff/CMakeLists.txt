# The cooperative task scheduler — a portable foundation library (HAL-free since
# timers_interrupts/fault_handler moved to the BSP). STATIC: it links after the
# app's OBJECT files, so the mutual references (app -> scheduler API, scheduler ->
# app allocator/log) resolve against the always-present app objects, same as
# deluge_dsp does.
file(GLOB_RECURSE OS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.S
        ${CMAKE_CURRENT_LIST_DIR}/*.c
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_scheduler STATIC ${OS_SOURCES})

set_target_properties(deluge_scheduler PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_include_directories(deluge_scheduler PUBLIC
        ${CMAKE_CURRENT_LIST_DIR} # scheduler_api.h
        ${SHARED_INCLUDE}         # OSLikeStuff/... paths
)
# The scheduler still reaches up for the app's allocator + debug log + the
# <libdeluge/...> boundary it now runs on (clock / system / storage_wait).
target_include_directories(deluge_scheduler PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../deluge
        ${CMAKE_SOURCE_DIR}/include
)
