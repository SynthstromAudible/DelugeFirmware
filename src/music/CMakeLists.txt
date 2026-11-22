# deluge_music: note sets, the preset scales, keys, scale mapping and note-length names. Standard library only
# (plus base/panic.h and the plugin/types sync words); scripts/check_layers.py (its library in scripts/layers.toml)
# enforces that.
add_library(deluge_music STATIC
    musical_key.cpp
    note_length.cpp
    note_set.cpp
    preset_scales.cpp
    scale_change.cpp
    scale_mapper.cpp
    utils.cpp
)
# Scale and key queries run per gesture or per note-on, not per sample.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_music)
# Headers are included as "music/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_music PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_music PUBLIC cxx_std_23)
