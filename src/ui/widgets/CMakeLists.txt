
# deluge_ui_widgets_core / deluge_ui_widgets_oled / deluge_ui_widgets_grid — the immediate-mode toolkit.
# Include only ui/**, ui/gfx/** and the standard library; oled and grid never include each other.
add_library(deluge_ui_widgets_core STATIC
  core/repaint_gate.cpp
  core/blink.cpp
  core/led_frame.cpp
  core/knob_indicator.cpp
  core/text_wrap.cpp
)
target_include_directories(deluge_ui_widgets_core PUBLIC ${SHARED_INCLUDE})
# See src/ui/gfx/CMakeLists.txt for what DELUGE_COMMON_COMPILE_OPTIONS carries and why it is PRIVATE.
target_compile_options(deluge_ui_widgets_core PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
# gfx_oled only: nothing under core/** includes anything from ui/gfx/grid (PadMask names
# kMaxGridCols in a comment, not in code), and carrying the dep here dragged gfx-grid into every
# ui-core consumer's link closure. deluge_ui_widgets_grid links it directly.
target_link_libraries(deluge_ui_widgets_core PUBLIC deluge_ui_gfx_oled)
set_target_properties(deluge_ui_widgets_core PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_widgets_oled STATIC
  oled/frame.cpp
  oled/text.cpp
  oled/scroll_text.cpp
  oled/list.cpp
  oled/text_edit.cpp
  oled/values.cpp
  oled/meters.cpp
  oled/threshold_meter.cpp
  oled/column_drawings.cpp
  oled/page_drawing.cpp
  oled/columns.cpp
  oled/popup.cpp
  oled/misc.cpp
  oled/routing.cpp
  oled/sample_position.cpp
  oled/rainfall.cpp
  oled/starfield.cpp
)
target_include_directories(deluge_ui_widgets_oled PUBLIC ${SHARED_INCLUDE})
target_compile_options(deluge_ui_widgets_oled PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_widgets_oled PUBLIC deluge_ui_widgets_core deluge_ui_gfx_oled)
set_target_properties(deluge_ui_widgets_oled PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
add_library(deluge_ui_widgets_grid STATIC
  grid/columns.cpp
  grid/effects.cpp
  grid/favourites.cpp
  grid/frame.cpp
  grid/keyboard.cpp
  grid/markers.cpp
  grid/rows.cpp
  grid/sidebar.cpp
  grid/waveform.cpp
  grid/zoom_transition.cpp
)
target_include_directories(deluge_ui_widgets_grid PUBLIC ${SHARED_INCLUDE})
target_compile_options(deluge_ui_widgets_grid PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_widgets_grid PUBLIC deluge_ui_widgets_core deluge_ui_gfx_grid)
set_target_properties(deluge_ui_widgets_grid PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
