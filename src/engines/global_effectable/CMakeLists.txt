# deluge_global_effectable: the params the song master, a kit's affect-entire and an audio track share, as one
# provisional table until each effect declares its own. Header-only, on the engine types only.
add_library(deluge_global_effectable INTERFACE)
target_include_directories(deluge_global_effectable INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_global_effectable INTERFACE cxx_std_23)
target_link_libraries(deluge_global_effectable INTERFACE deluge_engine_types)
