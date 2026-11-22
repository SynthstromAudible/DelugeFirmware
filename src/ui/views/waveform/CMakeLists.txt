# deluge_ui_views_waveform: a waveform view's columns, peaks and navigation, with no sample behind them. Header-only, standard
# library only; scripts/check_layers.py enforces that (scripts/layers.toml, library `ui-views-waveform`).
add_library(deluge_ui_views_waveform INTERFACE)
# Headers are included as "ui/views/waveform/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_ui_views_waveform INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_ui_views_waveform INTERFACE cxx_std_23)
