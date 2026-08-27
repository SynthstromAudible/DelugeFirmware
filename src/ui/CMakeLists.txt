# deluge_ui_core / deluge_ui_oled / deluge_ui_grid — the immediate-mode toolkit (spec B).
# Include only ui/**, gfx/** and the standard library; oled and grid never include each other.
add_library(deluge_ui_core STATIC
  core/repaint_gate.cpp
  core/led_frame.cpp
)
target_include_directories(deluge_ui_core PUBLIC ${SHARED_INCLUDE})
target_link_libraries(deluge_ui_core PUBLIC deluge_gfx_oled deluge_gfx_grid)
set_target_properties(deluge_ui_core PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_oled STATIC
  oled/frame.cpp
  oled/text.cpp
  oled/scroll_text.cpp
  oled/list.cpp
  oled/values.cpp
)
target_include_directories(deluge_ui_oled PUBLIC ${SHARED_INCLUDE})
target_link_libraries(deluge_ui_oled PUBLIC deluge_ui_core deluge_gfx_oled)
set_target_properties(deluge_ui_oled PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_grid INTERFACE)
target_link_libraries(deluge_ui_grid INTERFACE deluge_ui_core)
