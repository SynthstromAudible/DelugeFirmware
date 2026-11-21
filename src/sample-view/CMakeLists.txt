# deluge_sample_view: a waveform view's columns, peaks and navigation, with no sample behind them. Header-only, standard
# library only; scripts/check_layers.py enforces that (scripts/layers.toml, library `sample-view`).
add_library(deluge_sample_view INTERFACE)
# Headers are included as "sample-view/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_sample_view INTERFACE ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_sample_view INTERFACE cxx_std_23)
