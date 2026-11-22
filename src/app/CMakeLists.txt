# deluge_app_ports_waveforms: the host's cache of a waveform's columns behind the waveforms port. Header-only,
# standard library only (scripts/layers.toml, library `app-ports-waveforms`).
add_library(deluge_app_ports_waveforms INTERFACE)
# Headers are included as "app/ports/waveforms/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_app_ports_waveforms INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_app_ports_waveforms INTERFACE cxx_std_23)
target_link_libraries(deluge_app_ports_waveforms INTERFACE deluge_ui_views_waveform)
