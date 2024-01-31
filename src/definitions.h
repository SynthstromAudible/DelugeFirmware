#pragma once
#include "RZA1/cpu_specific.h"
#include "RZA1/system/r_typedefs.h"
#include "fault_handler.h" // IWYU pragma: export (for expanding the freeze with error macro)

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
// this is defined in deluge.cpp
enum DebugPrintMode { kDebugPrintModeDefault, kDebugPrintModeRaw, kDebugPrintModeNewlined };
void logDebug(enum DebugPrintMode mode, const char* file, int line, size_t bufsize, const char* format, ...);
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
#define TIMER_MIDI_GATE_OUTPUT 2
#define TIMER_SYSTEM_FAST 0
#define TIMER_SYSTEM_SLOW 4
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

// #define PLACE_SDRAM_TEXT __attribute__((__section__(".sdram_text"))) // Paul: I had problems with execution from
// SDRAM, maybe timing?

#if ENABLE_TEXT_OUTPUT
#define D_PRINTLN(...) logDebug(kDebugPrintModeNewlined, __FILE__, __LINE__, 256, __VA_ARGS__)
#define D_PRINT(...) logDebug(kDebugPrintModeDefault, __FILE__, __LINE__, 256, __VA_ARGS__)
#define D_PRINT_RAW(...) logDebug(kDebugPrintModeRaw, __FILE__, __LINE__, 256, __VA_ARGS__)

#else

#define D_PRINTLN(...)
#define D_PRINT(...)
#define D_PRINT_RAW(...)
#endif
