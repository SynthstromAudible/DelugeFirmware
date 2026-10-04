# deluge_instrument: the Deluge instrument (today's whole sound pipeline as one engine) — its state, its words and its
# codec. Built on the engine API only.
add_library(deluge_instrument INTERFACE)
target_include_directories(deluge_instrument INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_instrument INTERFACE cxx_std_23)
target_link_libraries(deluge_instrument INTERFACE deluge_engine_api)
