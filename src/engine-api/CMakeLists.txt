# deluge_engine_api: the host–engine contract's interface (descriptor, state codec shape), on the types layer
# (types/, its own target, added before this one).
add_library(deluge_engine_api INTERFACE)
target_include_directories(deluge_engine_api INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_engine_api INTERFACE cxx_std_23)
target_link_libraries(deluge_engine_api INTERFACE deluge_engine_types deluge_serialization)
