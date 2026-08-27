# deluge_ui_core / deluge_ui_oled / deluge_ui_grid — the immediate-mode toolkit (spec B).
# Include only ui/**, gfx/** and the standard library; oled and grid never include each other.
add_library(deluge_ui_core INTERFACE)
target_include_directories(deluge_ui_core INTERFACE ${SHARED_INCLUDE})
target_link_libraries(deluge_ui_core INTERFACE deluge_gfx_oled deluge_gfx_grid)
add_library(deluge_ui_oled INTERFACE)
target_link_libraries(deluge_ui_oled INTERFACE deluge_ui_core)
add_library(deluge_ui_grid INTERFACE)
target_link_libraries(deluge_ui_grid INTERFACE deluge_ui_core)
