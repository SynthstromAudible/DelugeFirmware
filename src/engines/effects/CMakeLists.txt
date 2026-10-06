# deluge_effects: the effects' words, their settings as plain state, and the codec of an owner element's effect
# entries. Built on the engine API only; serves the Deluge instrument's per-sound effects and the host chains.
add_library(deluge_effects INTERFACE)
target_include_directories(deluge_effects INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_effects INTERFACE cxx_std_23)
target_link_libraries(deluge_effects INTERFACE deluge_engine_api)
