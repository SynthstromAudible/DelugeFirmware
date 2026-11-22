# deluge_ui_controls — the controls' vocabulary: what a gold knob points at, as addresses into the model
# (deluge::ui::controls).
# It includes only the model (and the serialization values a written setting carries) and the plugin API;
# scripts/check_layers.py (its library in scripts/layers.toml) holds it to that.

add_library(deluge_ui_controls STATIC board.cpp knob_focus.cpp page.cpp pages.cpp resolve.cpp section.cpp takeover.cpp knob_math.cpp learned.cpp follow.cpp)
# Headers are included as "ui/controls/..."; the include root is src/, two levels up.
target_include_directories(deluge_ui_controls PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_ui_controls PUBLIC cxx_std_23)
target_link_libraries(deluge_ui_controls PUBLIC deluge_project deluge_plugin)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deluge_library.cmake)
deluge_library_target(deluge_ui_controls)
