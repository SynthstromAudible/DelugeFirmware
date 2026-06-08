# bsp-rza1-legacy: implements the <libdeluge/...> boundary for the Synthstrom
# Deluge (Renesas RZ/A1L) by wrapping the existing RZA1 HAL. Grows one port at a
# time as application subsystems are moved behind the boundary.

add_library(bsp_rza1 STATIC
    clock.c
    system.c
    signals.c
    display.c
    midi_io.c
    flash.c
    memory.c
    block_device.c
    cv_gate.c
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

# ---------------------------------------------------------------------------
# Legacy RZ/A1L drivers, relocated here from src/deluge/drivers (they ARE the
# board's driver layer). They still reference application headers (deluge.h,
# util/, gui/colour) and define interrupt handlers that are only referenced
# indirectly, so for now they are compiled directly into the `deluge` target
# (exactly as before the move, and as OSLikeStuff does) rather than archived
# into bsp_rza1 — this guarantees identical inclusion and avoids an archive
# pruning unreferenced ISR objects. They migrate into bsp_rza1 proper as each
# is reworked into a <libdeluge/...> port implementation.
# Note: drivers/usb/userdef stays under src/deluge (it is RZ/A1L USB *config*
# consumed by the RZA1 HAL itself, not a driver).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE bsp_rza1_driver_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.S
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.cpp
)
target_sources(deluge PUBLIC ${bsp_rza1_driver_SOURCES})

# Port implementations that depend on application types via the legacy drivers
# (e.g. control_surface.cpp uses RGB/definitions_cxx through the PIC class) are
# likewise compiled into `deluge` until the underlying driver is decoupled, at
# which point they move into the bsp_rza1 library beside clock.c/system.c.
target_sources(deluge PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/control_surface.cpp
    ${CMAKE_CURRENT_LIST_DIR}/display_freeze.cpp
)

# Resolve `#include "drivers/..."` to the relocated headers (for both the
# drivers themselves and the application code that still uses them directly).
target_include_directories(deluge PUBLIC ${CMAKE_CURRENT_LIST_DIR})
