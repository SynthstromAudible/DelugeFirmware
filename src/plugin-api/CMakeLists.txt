# deluge_plugin_api: the host–plugin contract's interface (descriptor, state codec shape), on the types layer
# (types/, its own target, added before this one).
add_library(deluge_plugin_api INTERFACE)
target_include_directories(deluge_plugin_api INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_plugin_api INTERFACE cxx_std_23)
target_link_libraries(deluge_plugin_api INTERFACE deluge_plugin_types deluge_serialization)
