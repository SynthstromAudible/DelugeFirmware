# deluge_controls — the controls' vocabulary: what a gold knob points at, as addresses into the model (deluge::controls).
# It includes only the model and the plugin API; scripts/check_library_boundary.py holds it to that. It runs on the UI
# tier, off the audio path, so it is built like the other model-side libraries (no unity build, no ARM-mode override):
# the unit trees build it too, and they carry neither DELUGE_HOST nor -marm.
add_library(deluge_controls STATIC knob_focus.cpp)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_controls)
# Headers are included as "controls/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_controls PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_controls PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_controls PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                   CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_controls PUBLIC deluge_project deluge_plugin_api)
target_compile_options(deluge_controls PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/library_warnings.cmake)
deluge_library_warnings(deluge_controls)
