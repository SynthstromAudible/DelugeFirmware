# deluge_ui_views_grid — whole pad pictures, each drawn from a plain description of what it shows. Includes only
# ui/widgets/grid, ui/widgets/core, ui/gfx/grid and the standard library (scripts/layers.toml).
add_library(deluge_ui_views_grid STATIC
  blink.cpp
  favourites.cpp
  keyboard.cpp
  marker.cpp
  pad_image.cpp
  playhead.cpp
  slicer.cpp
  waveform.cpp
  zoom_slide.cpp
)
target_include_directories(deluge_ui_views_grid PUBLIC ${SHARED_INCLUDE})
# See src/ui/gfx/CMakeLists.txt for what DELUGE_COMMON_COMPILE_OPTIONS carries and why it is PRIVATE.
target_compile_options(deluge_ui_views_grid PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_views_grid PUBLIC deluge_ui_widgets_grid)
set_target_properties(deluge_ui_views_grid PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
