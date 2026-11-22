# deluge_plugin: the host–plugin contract's interface (descriptor, state codec shape), on the types layer
# (types/, its own target, added before this one).
add_library(deluge_plugin INTERFACE)
target_include_directories(deluge_plugin INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_plugin INTERFACE cxx_std_23)
target_link_libraries(deluge_plugin INTERFACE deluge_plugin_types deluge_serialization)
