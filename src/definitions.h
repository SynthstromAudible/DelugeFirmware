#pragma once
#include "board_config.h"     // board capabilities/layout (HAL-free; was RZA1/cpu_specific.h)
#include "foundation/panic.h" // IWYU pragma: export — freezeWithError + FREEZE_WITH_ERROR (foundation tier)
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

// freezeWithError() + the FREEZE_WITH_ERROR macro now live in <foundation/panic.h>
// (the foundation tier), pulled in above.

#define RUNTIME_FEATURE_SETTING_MAX_OPTIONS 9

// Board-specific values (audio/MIDI buffer sizes, CACHE_LINE_SIZE, PLACE_SDRAM_*, XTAL,
// timer/channel counts) now live in board_config.h (above). Raw RZ/A1L peripheral indices
// (the MTU TIMER_* channels) live in the HAL, RZA1/cpu_specific.h.
