#pragma once
#include "OSLikeStuff/fault_handler/fault_handler.h" // IWYU pragma: export (for expanding the freeze with error macro)
#include "board_config.h"                            // board capabilities/layout (HAL-free; was RZA1/cpu_specific.h)
// (Also was RZA1/system/r_typedefs.h, a Renesas wrapper around these standard headers.)
#include <stdbool.h> // IWYU pragma: export
#include <stddef.h>  // IWYU pragma: export
#include <stdint.h>  // IWYU pragma: export

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

#if !defined(NDEBUG)
#define ENABLE_SEQUENTIALITY_TESTS 1
#else
#define ENABLE_SEQUENTIALITY_TESTS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif
// This is defined in display.cpp
extern void freezeWithError(char const* errmsg);
#ifdef __cplusplus
}
#endif
#if defined(__arm__)
#define FREEZE_WITH_ERROR(error)                                                                                       \
	({                                                                                                                 \
		uint32_t regLR = 0;                                                                                            \
		uint32_t regSP = 0;                                                                                            \
		asm volatile("MOV %0, LR\n" : "=r"(regLR));                                                                    \
		asm volatile("MOV %0, SP\n" : "=r"(regSP));                                                                    \
		fault_handler_print_freeze_pointers(0, 0, regLR, regSP);                                                       \
		freezeWithError(error);                                                                                        \
	})
#else
#define FREEZE_WITH_ERROR(error) ({ freezeWithError(error); })
#endif

#define RUNTIME_FEATURE_SETTING_MAX_OPTIONS 9

// Board-specific values (audio/MIDI buffer sizes, CACHE_LINE_SIZE, PLACE_SDRAM_*, XTAL,
// timer/channel counts) now live in board_config.h (above). Raw RZ/A1L peripheral indices
// (the MTU TIMER_* channels) live in the HAL, RZA1/cpu_specific.h.
