# deluge_gfx_oled / deluge_gfx_grid — pure drawing, no policy, no globals, no hardware.
# Depends on nothing but the C/C++ standard library. Included as <gfx/oled/...> and
# <gfx/grid/...> via the universal `-I src` path. Rules: src/gfx/README.md; enforced by
# scripts/check_gfx_layer.py (a ctest).
add_library(deluge_gfx_oled STATIC oled/font.cpp oled/fonts/fonts.c)
target_include_directories(deluge_gfx_oled PUBLIC ${SHARED_INCLUDE})
set_target_properties(deluge_gfx_oled PROPERTIES C_STANDARD 23 CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
add_library(deluge_gfx_grid INTERFACE)
target_include_directories(deluge_gfx_grid INTERFACE ${SHARED_INCLUDE})
