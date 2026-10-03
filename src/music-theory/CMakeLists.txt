# deluge_music_theory: note sets, the preset scales, keys and scale mapping. Standard library only (plus
# foundation/panic.h); scripts/check_library_boundary.py enforces that with no allowlist.
add_library(deluge_music_theory STATIC
    musical_key.cpp
    note_set.cpp
    preset_scales.cpp
    scale_change.cpp
    scale_mapper.cpp
    utils.cpp
)
# Headers are included as "music-theory/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_music_theory PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_music_theory PUBLIC cxx_std_23)
