#pragma once
#include "board_config.h"     // board capabilities/layout (HAL-free; was RZA1/cpu_specific.h)
#include "foundation/panic.h" // IWYU pragma: export — freezeWithError + FREEZE_WITH_ERROR (foundation tier)
// (Also was RZA1/system/r_typedefs.h, a Renesas wrapper around these standard headers.)
#include <stdbool.h> // IWYU pragma: export
#include <stddef.h>  // IWYU pragma: export
#include <stdint.h>  // IWYU pragma: export

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

// Runtime heap-corruption / use-after-free detector built into the custom allocator. Gated separately from
// ALPHA_OR_BETA_VERSION so it can be toggled (or compiled out entirely) independently of other beta checks. When 0, all
// of the guard machinery (boundary-tag validation, free poisoning, the allocation side table, redzones, and the
// periodic heap walk) compiles away to nothing.
//
// Disabled automatically under a sanitizer (host-sim AddressSanitizer, or DELUGE_VALGRIND): MEM_GUARD writes and reads
// the freed block body (its poison fill + verify-on-realloc), which would itself trip the sanitizer. When a sanitizer is
// active the heap_poison.h hooks own corruption detection instead - they give strictly stronger, hardware-accurate
// coverage and must be the sole writer of freed-block shadow state.
#if defined(__SANITIZE_ADDRESS__) || defined(DELUGE_VALGRIND)
#define MEM_GUARD 0
#else
#define MEM_GUARD ALPHA_OR_BETA_VERSION
#endif

// Reference-graph sanity for the Cluster "reasons" system (see memory/reason_check.h): asserts a Stealable Cluster is
// never stolen while still referenced, and can report Clusters that stay pinned (reason leaks). Pure bookkeeping - it
// never touches freed memory - so unlike MEM_GUARD it stays enabled under a sanitizer and complements the host-sim ASan
// build.
#define REASON_CHECK ALPHA_OR_BETA_VERSION

// Fills the whole body of a block when it is freed. A use-after-free *write* breaks this pattern; reads of freed memory
// show a recognizable value. Never-allocated memory is left as the startup-cleared zero, so an EMPTY block is always
// uniformly poison (if it has ever been freed) or zero (if it never has) - both of which the detector treats as clean.
// This uniformity is what lets verify-on-realloc validate an arbitrary sub-range carved out of a freed block.
#define MEM_GUARD_FREE_POISON 0xDEADBEEFu
// Fills the tail slack of every ALLOCATED block (the gap padSize() leaves between the requested and allocated sizes).
// A small tail overrun that doesn't reach the next block's header still corrupts this.
#define MEM_GUARD_REDZONE 0xBADCAFE5u
// How many AudioEngine::routine() calls between full periodic heap walks.
#define MEM_GUARD_WALK_INTERVAL 1024u

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
