# deluge_gfx_oled / deluge_gfx_grid — pure drawing, no policy, no globals, no hardware.
# Depends on nothing but the C/C++ standard library. Included as <gfx/oled/...> and
# <gfx/grid/...> via the universal `-I src` path. Rules: src/gfx/README.md; enforced by
# scripts/check_gfx_layer.py (a ctest).
add_library(deluge_gfx_oled STATIC oled/canvas.cpp oled/font.cpp oled/fonts/fonts.c oled/icons.cpp)
target_include_directories(deluge_gfx_oled PUBLIC ${SHARED_INCLUDE})
# The project's warning set (-Werror=switch, -Warray-bounds, -Wstack-usage, and -Wall/-Wextra
# under MAINTAINER_MODE). Defined in cmake/deluge_common_options.cmake, which the device, sim
# and tests projects all include, so these flags apply wherever this target is built. PRIVATE:
# a consumer's own warning policy is its own business.
target_compile_options(deluge_gfx_oled PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_gfx_oled PROPERTIES C_STANDARD 23 CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
add_library(deluge_gfx_grid STATIC grid/rgb.cpp grid/sine_table.cpp grid/exp_table.cpp grid/pad_grid.cpp grid/tween.cpp grid/composite.cpp)
target_include_directories(deluge_gfx_grid PUBLIC ${SHARED_INCLUDE})
target_compile_options(deluge_gfx_grid PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})
set_target_properties(deluge_gfx_grid PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
