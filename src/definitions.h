#pragma once

#include "RZA1/cpu_specific.h"
#include "RZA1/system/r_typedefs.h"

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

// #define PLACE_SDRAM_TEXT __attribute__((__section__(".sdram_text"))) // Paul: I had problems with execution from SDRAM, maybe timing?
