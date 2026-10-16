# The level-3 runner on a fake host. Its own driver: the spec defines the host and BSP functions the runner calls,
# which no other spec executable may also define.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../harness/level3 level3)
deluge_specs_driver(level3_runner_specs LIBRARIES deluge_ui_level3)
