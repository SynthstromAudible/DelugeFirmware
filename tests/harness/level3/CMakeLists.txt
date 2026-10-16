# The level-3 runner: the script pump, its name tables and its snapshot writers. A host links the BSP half the runner
# calls (host_script_push_event, the shadows) and provides host_script_now_ms and host_script_exit.
add_library(deluge_ui_level3 STATIC
    host_script.c
    host_script_tables.c
    host_snapshot.c
    ${CMAKE_CURRENT_LIST_DIR}/../ui_script.cpp
)
target_include_directories(deluge_ui_level3 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../..
    ${CMAKE_CURRENT_LIST_DIR}/../../../include
)
set_target_properties(deluge_ui_level3 PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED YES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
)
