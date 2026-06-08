# bsp-rza1-legacy: implements the <libdeluge/...> boundary for the Synthstrom
# Deluge (Renesas RZ/A1L) by wrapping the existing RZA1 HAL. Grows one port at a
# time as application subsystems are moved behind the boundary.

add_library(bsp_rza1 STATIC
    clock.c
    system.c
)

# Public: the libdeluge headers this BSP implements (propagated to consumers so
# the application can include <libdeluge/...>).
target_include_directories(bsp_rza1 PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Private: the HAL and shared config. The BSP may include RZA1/*; nothing above
# the boundary may.
target_include_directories(bsp_rza1 PRIVATE
    ${SHARED_INCLUDE}
)

target_link_libraries(bsp_rza1 PUBLIC RZA1)

# Link the BSP into the firmware. PUBLIC so the libdeluge include dir propagates
# to the deluge target's own translation units.
target_link_libraries(deluge PUBLIC bsp_rza1)
