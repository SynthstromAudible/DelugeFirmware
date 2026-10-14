# deluge_ui_core / deluge_ui_oled / deluge_ui_grid — the immediate-mode toolkit (spec B).
# Include only ui/**, gfx/** and the standard library; oled and grid never include each other.
add_library(deluge_ui_core STATIC
  core/repaint_gate.cpp
  core/led_frame.cpp
  core/knob_indicator.cpp
  core/text_wrap.cpp
)
target_include_directories(deluge_ui_core PUBLIC ${SHARED_INCLUDE})
# See src/gfx/CMakeLists.txt for what DELUGE_COMMON_COMPILE_OPTIONS carries and why it is PRIVATE.
target_compile_options(deluge_ui_core PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
# gfx_oled only: nothing under core/** includes anything from gfx/grid (PadMask names
# kMaxGridCols in a comment, not in code), and carrying the dep here dragged gfx-grid into every
# ui-core consumer's link closure. deluge_ui_grid links it directly.
target_link_libraries(deluge_ui_core PUBLIC deluge_gfx_oled)
set_target_properties(deluge_ui_core PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_oled STATIC
  oled/frame.cpp
  oled/text.cpp
  oled/scroll_text.cpp
  oled/list.cpp
  oled/text_edit.cpp
  oled/values.cpp
  oled/meters.cpp
  oled/columns.cpp
  oled/popup.cpp
  oled/misc.cpp
  oled/routing.cpp
  oled/sample_position.cpp
  oled/rainfall.cpp
  oled/starfield.cpp
)
target_include_directories(deluge_ui_oled PUBLIC ${SHARED_INCLUDE})
target_compile_options(deluge_ui_oled PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_oled PUBLIC deluge_ui_core deluge_gfx_oled)
set_target_properties(deluge_ui_oled PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_grid STATIC
  grid/columns.cpp
  grid/effects.cpp
  grid/frame.cpp
  grid/keyboard.cpp
  grid/markers.cpp
  grid/rows.cpp
  grid/sidebar.cpp
  grid/waveform.cpp
)
target_include_directories(deluge_ui_grid PUBLIC ${SHARED_INCLUDE})
target_compile_options(deluge_ui_grid PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_grid PUBLIC deluge_ui_core deluge_gfx_grid)
set_target_properties(deluge_ui_grid PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
