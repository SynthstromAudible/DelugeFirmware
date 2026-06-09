# deluge_foundation — the foundation tier.
#
# Cross-cutting, dependency-free primitives usable by the application, the BSP and
# the HAL alike: pure number formatting, board timer-count math, and (header-only)
# the panic contract. It depends on nothing but libc and the HAL-free
# board_config.h, so it sits at the bottom of the stack
#   app -> BSP/libdeluge -> HAL -> foundation
# and the layers above reach *down* into it (never the reverse). Its headers live
# under src/foundation and resolve via the universal `-I src` path, so even
# vendored code and the everywhere-included definitions.h can use them.
add_library(deluge_foundation STATIC
    string_format.c
    timer_count.c
)

target_include_directories(deluge_foundation PUBLIC ${SHARED_INCLUDE})
