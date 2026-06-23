#pragma once
#include "OSLikeStuff/fault_handler/fault_handler.h" // IWYU pragma: export (for expanding the freeze with error macro)
#include "RZA1/cpu_specific.h"
#include "RZA1/system/r_typedefs.h"

#define ALPHA_OR_BETA_VERSION 1 // Whether to compile with additional error-checking

// Runtime heap-corruption / use-after-free detector built into the custom allocator. Gated separately from
// ALPHA_OR_BETA_VERSION so it can be toggled (or compiled out entirely) independently of other beta checks. When 0, all
// of the guard machinery (boundary-tag validation, free poisoning, the allocation side table, redzones, and the
// periodic heap walk) compiles away to nothing.
#define MEM_GUARD ALPHA_OR_BETA_VERSION

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

/// Hardware timer index used for MIDI gate clock
#define TIMER_MIDI_GATE_OUTPUT 2
/// Hardware timer index used for "fast" events.
///
/// Runs at 528 ticks per millisecond (528 kHz).
#define TIMER_SYSTEM_FAST 0
/// Hardware timer index used for "slow" events.
///
/// Runs at 32 ticks per millisecond (32 kHz).
#define TIMER_SYSTEM_SLOW 4
/// Hardware timer for "superfast" events.
///
/// Runs as 32.792 ticks per microsecond (33.792 MHz)
#define TIMER_SYSTEM_SUPERFAST 1

#define SSI_TX_BUFFER_NUM_SAMPLES 128
#define SSI_RX_BUFFER_NUM_SAMPLES 2048
#define NUM_MONO_INPUT_CHANNELS (NUM_STEREO_INPUT_CHANNELS * 2)
#define NUM_MONO_OUTPUT_CHANNELS (NUM_STEREO_OUTPUT_CHANNELS * 2)

#define TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED 4

#define CACHE_LINE_SIZE 32

#if DELUGE_MODEL >= DELUGE_MODEL_144_G
#define XTAL_SPEED_MHZ 13007402 // 1.65% lower, for SSCG
#else
#define XTAL_SPEED_MHZ 13225625
#endif

#define RUNTIME_FEATURE_SETTING_MAX_OPTIONS 9

// UART
#define MIDI_TX_BUFFER_SIZE 1024

#define MIDI_RX_BUFFER_SIZE 512
#define MIDI_RX_TIMING_BUFFER_SIZE 32 // Must be <= MIDI_RX_BUFFER_SIZE, above

#define MAX_NUM_USB_MIDI_DEVICES 6

// Paul: It seems this area is not executable, could not find a reason in the datasheet, marked NOLOAD now
#define PLACE_INTERNAL_FRUNK __attribute__((__section__(".frunk_bss")))

#define PLACE_SDRAM_BSS __attribute__((__section__(".sdram_bss")))
#define PLACE_SDRAM_DATA __attribute__((__section__(".sdram_data")))
#define PLACE_SDRAM_RODATA __attribute__((__section__(".sdram_rodata")))
// #define PLACE_SDRAM_TEXT __attribute__((__section__(".sdram_text"))) // Paul: I had problems with execution from
// SDRAM, maybe timing?
