# Board support packages (BSPs). Each BSP implements the <libdeluge/...> C-ABI
# boundary for one board and is the only consumer of that board's HAL.
# See docs/dev/libdeluge_bsp_design.md.
add_subdirectory(rza1)
