# deluge_ui_views_oled — whole OLED screens, each drawn from a plain description of what it shows. Includes only
# ui/widgets/oled, ui/widgets/core, ui/gfx/oled and the standard library (scripts/layers.toml).
add_library(deluge_ui_views_oled STATIC
  detail/layout.cpp
  session.cpp
)
target_include_directories(deluge_ui_views_oled PUBLIC ${SHARED_INCLUDE})
# See src/ui/gfx/CMakeLists.txt for what DELUGE_COMMON_COMPILE_OPTIONS carries and why it is PRIVATE.
target_compile_options(deluge_ui_views_oled PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
target_link_libraries(deluge_ui_views_oled PUBLIC deluge_ui_widgets_oled)
set_target_properties(deluge_ui_views_oled PROPERTIES
  CXX_STANDARD 26
  CXX_STANDARD_REQUIRED YES
)
