# bsp-rza1-legacy: implements the <libdeluge/...> boundary for the Synthstrom
# Deluge (Renesas RZ/A1L) by wrapping the existing RZA1 HAL. Grows one port at a
# time as application subsystems are moved behind the boundary.

add_library(bsp_rza1 STATIC
    clock.c
    system.c
    signals.c
    board.c
    display.c
    midi_io.c
    usb_midi.c
    flash.c
    memory.c
    block_device.c
    cv_gate.c
    # App-header-free port impls that drive the PIC (pic.h is now byte-oriented /
    # app-free). They still reference legacy-driver symbols compiled into the
    # `deluge` target, resolved at the final link — same as midi_io.c/cv_gate.c.
    control_surface.cpp
    display_freeze.cpp
)

# Public: the libdeluge headers this BSP implements (propagated to consumers so
# the application can include <libdeluge/...>).
target_include_directories(bsp_rza1 PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Private: the HAL and shared config. The BSP may include RZA1/*; nothing above
# the boundary may. CMAKE_CURRENT_LIST_DIR (src/bsp/rza1) resolves `#include
# "drivers/..."` from the lib's own port impls.
target_include_directories(bsp_rza1 PRIVATE
    ${SHARED_INCLUDE}
    ${CMAKE_CURRENT_LIST_DIR}
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
# Note: drivers/usb/userdef/r_usb_pmidi_config.h (RZ/A1L USB-MIDI *config*,
# consumed by the RZA1 HAL and the usb_midi port) now lives here under
# drivers/usb/userdef, out of the application tree.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE bsp_rza1_driver_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.S
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/*.cpp
)
target_sources(deluge PUBLIC ${bsp_rza1_driver_SOURCES})

# Resolve `#include "drivers/..."` to the relocated headers (for both the
# drivers themselves and the application code that still uses them directly).
target_include_directories(deluge PUBLIC ${CMAKE_CURRENT_LIST_DIR})
