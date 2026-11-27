# deluge_ui_scenes_bind — what a scenes menu node edits, bound to the model's records and the engines' descriptors.
#
# Standalone of the app: it depends on the model and the engine libraries only, so its specs (tests/unit/ui/scenes/bind)
# run against a project::Song with no engine runtime objects, and scripts/check_layers.py (its library in
# scripts/layers.toml) holds it to that.
# The app supplies what it cannot see: the sink that applies Edits (WriteSink) and the ports
# (ports/), one interface per domain.

add_library(deluge_ui_scenes_bind STATIC dispatch.cpp slot_reader.cpp chain_reader.cpp model_reader.cpp device_reader.cpp setting_context.cpp knob_assignment.cpp page_sources.cpp
                                      ports/param_info.cpp ports/device_settings.cpp)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_ui_scenes_bind)
# src/ for its own and the model's headers; src/deluge for the name table (gui/l10n/setting_names.h, whose definitions
# the app links, or a spec compiles in).
target_include_directories(deluge_ui_scenes_bind PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../.. ${CMAKE_CURRENT_LIST_DIR}/../../../deluge)
target_compile_features(deluge_ui_scenes_bind PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_ui_scenes_bind PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                      CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_ui_scenes_bind PUBLIC deluge_project deluge_ui_controls deluge_instruments_classic deluge_host_chains deluge_arp deluge_ui_views_waveform)
target_compile_options(deluge_ui_scenes_bind PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
