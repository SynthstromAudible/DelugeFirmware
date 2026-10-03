# deluge_engine_types: the data-only types of the host–engine contract. Header-only, standard library only;
# scripts/check_engine_types_boundary.py enforces that with no allowlist.
add_library(deluge_engine_types INTERFACE)
target_include_directories(deluge_engine_types INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_engine_types INTERFACE cxx_std_23)
