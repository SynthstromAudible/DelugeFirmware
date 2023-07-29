add_library(RTT STATIC
    SEGGER_RTT.c
    SEGGER_RTT_printf.c
    SEGGER_RTT_ASM_ARMv7M.S
)
target_include_directories(RTT PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
