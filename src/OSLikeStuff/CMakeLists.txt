# task_scheduler/clock_type.h: the C++ Time wrapper built on <libdeluge/scheduler.h>'s dTime /
# DELUGE_CLOCKS_PER*. The scheduler's own C ABI is declared there now; this only reaches clock_type.h.
add_library(deluge_scheduler INTERFACE)
target_include_directories(deluge_scheduler INTERFACE
        ${CMAKE_CURRENT_LIST_DIR} # task_scheduler/clock_type.h
        ${SHARED_INCLUDE}         # OSLikeStuff/... paths
)
