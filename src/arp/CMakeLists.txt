# deluge_arp — the arpeggiator's params: one table for every arp's owner, and the values an arp plays by. Header-only;
# it depends on the plugin types alone, and scripts/check_layers.py (its library in scripts/layers.toml) holds it to
# that.
add_library(deluge_arp INTERFACE)
# Headers are included as "arp/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_arp INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_arp INTERFACE cxx_std_23)
target_link_libraries(deluge_arp INTERFACE deluge_plugin_types)
