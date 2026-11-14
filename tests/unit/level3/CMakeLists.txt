# The level-3 runner on a fake host. Its own driver: the spec defines the host and BSP functions the runner calls,
# which no other spec executable may also define.
deluge_specs_driver(level3_runner_specs TREES host LIBRARIES deluge_ui_level3)
