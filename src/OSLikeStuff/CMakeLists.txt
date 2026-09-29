# The scheduler's C API headers (scheduler_api.h, task_scheduler/*.h). The scheduler itself is the BSP's (the Rust
# BSP's scheduler.rs); the app and the BSP agree on it through these declarations.
add_library(deluge_scheduler INTERFACE)
target_include_directories(deluge_scheduler INTERFACE
        ${CMAKE_CURRENT_LIST_DIR} # scheduler_api.h
        ${SHARED_INCLUDE}         # OSLikeStuff/... paths
)
