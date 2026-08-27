# deluge_gfx_oled / deluge_gfx_grid — pure drawing, no policy, no globals, no hardware.
# Depends on nothing but the C/C++ standard library. Included as <gfx/oled/...> and
# <gfx/grid/...> via the universal `-I src` path. Rules: src/gfx/README.md; enforced by
# scripts/check_gfx_layer.py (a ctest).
add_library(deluge_gfx_oled INTERFACE)
add_library(deluge_gfx_grid INTERFACE)
foreach(lib deluge_gfx_oled deluge_gfx_grid)
    target_include_directories(${lib} INTERFACE ${SHARED_INCLUDE})
endforeach()
