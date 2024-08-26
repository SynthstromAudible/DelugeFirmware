/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef SYSTEM_CPU_SPECIFIC_H_
#define SYSTEM_CPU_SPECIFIC_H_

// RZ/A1

#define DELUGE_MODEL_40_PAD  0
#define DELUGE_MODEL_144_PAD 1

#define DELUGE_MODEL DELUGE_MODEL_144_PAD // Change this as needed

#define NUM_PHYSICAL_CV_CHANNELS 2
#define NUM_GATE_CHANNELS        4

#define DMA_LVL_FOR_SSI   (1 << 6)
#define DMARS_FOR_SSI0_TX 0x00E1
#define DMARS_FOR_SSI0_RX 0x00E2

#define DMA_AM_FOR_SCIF    (0b010 << 8)
#define DMARS_FOR_SCIF0_RX 0b001100010
#define DMARS_FOR_SCIF0_TX 0b001100001

#define DMARS_FOR_RSPI_TX 0b100100001

// UART ----------------------------------------------------
enum UartItemType {
    UART_ITEM_PIC,
    UART_ITEM_MIDI,
    NUM_UART_ITEMS,
};

// Aliases, cos multiple things on same PIC for this device
#define UART_ITEM_PIC_PADS       UART_ITEM_PIC
#define UART_ITEM_PIC_INDICATORS UART_ITEM_PIC

// PIC UART buffer sizes - different here on A1 because on A2 we have 3 of them
#define PIC_TX_BUFFER_SIZE                                                                                             \
    1024 // Theoretically I should be able to do just 512, and it almost works, but occasionally the cursor pos won't
         // update - at the end of an audio clip record
#define PIC_RX_BUFFER_SIZE 64

#define TIMING_CAPTURE_ITEM_MIDI 0
#define NUM_TIMING_CAPTURE_ITEMS 1

#define NUM_BUTTON_ROWS                4
#define NUM_BUTTON_COLS                9
#define NUM_LED_COLS                   9
#define SPI_CHANNEL_CV                 0
#define SPI_CHANNEL_OLED_MAIN          0
#define UART_CHANNEL_MIDI              0
#define UART_CHANNEL_PIC               1
#define UART_INITIAL_SPEED_PIC_PADS_HZ 31250
#define UI_MS_PER_REFRESH              50
#define UI_MS_PER_REFRESH_SCROLLING                                                                                    \
    7 // Any faster than this is faster than the UART bus can do, so there's a lag, and if we're sending out multiple
      // scrolls fast, the buffer can build up quite a lag

#define NUM_LED_ROWS 4

// SPI-BSC ---------------------------------------------------------------------
#define SPIBSC_CH 0

// SSI -------------------------------------------------------------------------
#define SSI_CHANNEL                        0
#define NUM_STEREO_INPUT_CHANNELS          1
#define NUM_STEREO_OUTPUT_CHANNELS         1
#define NUM_MONO_INPUT_CHANNELS_MAGNITUDE  1
#define NUM_MONO_OUTPUT_CHANNELS_MAGNITUDE 1

// SDHI
#define SD_PORT 1

// DMA --------------------------------------------------------------------------
/*
DMA channels:
0:
1:
2: SD host - defined in sd_cfg.h
3:
4: OLED and CV SPI (if OLED supported) - defined below
5:
6: audio TX - defined below
7: audio RX - defined below
8: SD SPI TX for 40-pad Deluge - defined in mmc.c
9: SD SPI RX for 40-pad Deluge
10: PIC UART TX - defined below
11: MIDI UART TX - defined below
12: PIC UART RX - defined below
13: MIDI UART RX - defined below
14: MIDI UART RX timing - defined below
15:
*/

#define OLED_SPI_DMA_CHANNEL 4

#define SSI_TX_DMA_CHANNEL 6
#define SSI_RX_DMA_CHANNEL 7

#define PIC_TX_DMA_CHANNEL  10
#define MIDI_TX_DMA_CHANNEL 11

#define PIC_RX_DMA_CHANNEL         12
#define MIDI_RX_DMA_CHANNEL        13
#define MIDI_RX_TIMING_DMA_CHANNEL 14

#define EXTERNAL_MEMORY_BEGIN       0x0C000000
#define EXTERNAL_MEMORY_END         0x10000000
#define UNUSED_MEMORY_SPACE_ADDRESS 0x08000000

#define INTERNAL_MEMORY_BEGIN 0x20000000uL

#define UNCACHED_MIRROR_OFFSET 0x40000000

#define NUM_LEVEL_INDICATORS 2

#ifndef HAVE_RTT
#define HAVE_RTT 0
#endif

// OLED
#define OLED_MAIN_WIDTH_PIXELS 128

// --- 64 pixels high
// #define OLED_MAIN_HEIGHT_PIXELS 64
// #define OLED_MAIN_TOPMOST_PIXEL 2
// #define OLED_HEIGHT_CHARS 6

// --- 48 pixels high
#define OLED_MAIN_HEIGHT_PIXELS 48
#define OLED_MAIN_TOPMOST_PIXEL 5
#define OLED_HEIGHT_CHARS       4

#define OLED_MAIN_VISIBLE_HEIGHT (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL)

#endif /* SYSTEM_CPU_SPECIFIC_H_ */
