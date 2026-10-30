# deluge_plugin_types: the data-only types of the host–plugin contract. Header-only, standard library only;
# scripts/check_library_boundary.py enforces that with no allowlist.
add_library(deluge_plugin_types INTERFACE)
target_include_directories(deluge_plugin_types INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_plugin_types INTERFACE cxx_std_23)
