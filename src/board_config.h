/*
 * Copyright © 2015-2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// board_config.h — Deluge board capabilities and layout.
///
/// "What the board *is*": pad/CV/gate counts, button and LED grid, display
/// geometry, channel layout, UI cadence. These are portable configuration the
/// application legitimately depends on, so they live here — a HAL-free header —
/// rather than in the RZ/A1L HAL. A different board replaces this file.
///
/// This header deliberately contains NO hardware-implementation detail (no DMA
/// channels, bus/port indices, register values or memory-map addresses). Those
/// live in the HAL (RZA1/cpu_specific.h); application code that still references
/// them is, by definition, not yet portable and is flagged by the platform-
/// boundary check until it moves behind a <libdeluge/...> port.
#ifndef DELUGE_BOARD_CONFIG_H
#define DELUGE_BOARD_CONFIG_H

// --- Model ------------------------------------------------------------------
#define DELUGE_MODEL_40_PAD 0
#define DELUGE_MODEL_144_PAD 1

#define DELUGE_MODEL DELUGE_MODEL_144_PAD // Change this as needed

// --- Capabilities -----------------------------------------------------------
#define NUM_PHYSICAL_CV_CHANNELS 2
#define NUM_GATE_CHANNELS 4

#define NUM_BUTTON_ROWS 4
#define NUM_BUTTON_COLS 9
#define NUM_LED_COLS 9
#define NUM_LED_ROWS 4
#define NUM_LEVEL_INDICATORS 2

#define NUM_STEREO_INPUT_CHANNELS 1
#define NUM_STEREO_OUTPUT_CHANNELS 1
#define NUM_MONO_INPUT_CHANNELS_MAGNITUDE 1
#define NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE 1

// --- UI cadence -------------------------------------------------------------
#define UI_MS_PER_REFRESH 50

// Any faster than this is faster than the UART bus can do, so there's a lag, and if we're sending out multiple
// scrolls fast, the buffer can build up quite a lag
#define UI_MS_PER_REFRESH_SCROLLING 7

// --- Display geometry (OLED) ------------------------------------------------
#define OLED_MAIN_WIDTH_PIXELS 128
#define OLED_MAIN_HEIGHT_PIXELS 48
#define OLED_MAIN_TOPMOST_PIXEL 5
#define OLED_HEIGHT_CHARS 4
#define OLED_MAIN_VISIBLE_HEIGHT (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL)

// Below: board-specific *values* for portable *concepts* (cache-line size, audio/MIDI
// buffer sizes, memory placement, clock). The values are board-set; the concepts apply to
// any board, so the application may use them. A different board (or a host build) supplies
// its own values here. This is NOT a place for raw peripheral/DMA/register indices — those
// live in the HAL (RZA1/cpu_specific.h).

// --- DMA-buffer alignment ---------------------------------------------------
#define CACHE_LINE_SIZE 32

// --- Audio / MIDI transport buffer sizes ------------------------------------
#define SSI_TX_BUFFER_NUM_SAMPLES 128
#define SSI_RX_BUFFER_NUM_SAMPLES 2048

#define NUM_MONO_INPUT_CHANNELS (NUM_STEREO_INPUT_CHANNELS * 2)
#define NUM_MONO_OUTPUT_CHANNELS (NUM_STEREO_OUTPUT_CHANNELS * 2)

#define MIDI_TX_BUFFER_SIZE 1024
#define MIDI_RX_BUFFER_SIZE 512
#define MIDI_RX_TIMING_BUFFER_SIZE 32 // Must be <= MIDI_RX_BUFFER_SIZE, above

#define MAX_NUM_USB_MIDI_DEVICES 6

#define TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED 4

// --- Clock ------------------------------------------------------------------
#if DELUGE_MODEL >= DELUGE_MODEL_144_G
#define XTAL_SPEED_MHZ 13007402 // 1.65% lower, for SSCG
#else
#define XTAL_SPEED_MHZ 13225625
#endif

// --- Memory placement -------------------------------------------------------
// The platform/linker provides these sections; a host build defines them as no-ops.
// Paul: It seems this area is not executable, could not find a reason in the datasheet,
// marked NOLOAD now
#if defined(__APPLE__) || IN_UNIT_TESTS
// Mach-O rejects ELF-style ".name" section specifiers (it wants "__SEG,__sect", <=16 chars), and a
// macOS build is always the host — which has no real SDRAM — so these placements are no-ops there.
// Unit tests are likewise a host build with no SDRAM, and no linker script providing the sections.
#define PLACE_INTERNAL_FRUNK
#define PLACE_SDRAM_BSS
#define PLACE_SDRAM_DATA
#define PLACE_SDRAM_RODATA
#define PLACE_SDRAM_TEXT
#else
#define PLACE_INTERNAL_FRUNK __attribute__((__section__(".frunk_bss")))

#define PLACE_SDRAM_BSS __attribute__((__section__(".sdram_bss")))
#define PLACE_SDRAM_DATA __attribute__((__section__(".sdram_data")))
#define PLACE_SDRAM_RODATA __attribute__((__section__(".sdram_rodata")))
#endif
// #define PLACE_SDRAM_TEXT __attribute__((__section__(".sdram_text"))) // Paul: I had
// problems with execution from SDRAM, maybe timing?

#endif /* DELUGE_BOARD_CONFIG_H */
