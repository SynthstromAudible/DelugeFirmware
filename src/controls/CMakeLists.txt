# deluge_controls — the controls' vocabulary: what a gold knob points at, as addresses into the model (deluge::controls).
# It includes only the model and the plugin API; scripts/check_library_boundary.py holds it to that.

add_library(deluge_controls STATIC knob_focus.cpp page.cpp resolve.cpp)
# Headers are included as "controls/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_controls PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_controls PUBLIC cxx_std_23)
target_link_libraries(deluge_controls PUBLIC deluge_project deluge_plugin_api)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deluge_library.cmake)
deluge_library_target(deluge_controls)
