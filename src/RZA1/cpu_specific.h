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

// RZ/A1L hardware-implementation constants: register values, DMA channels, bus
// and port indices, buffer sizes and the memory map. These are HAL/BSP-internal
// and must NOT be referenced by the portable application — code that includes
// this header is, by that fact, hardware-coupled.
//
// Board *capabilities* (counts, dimensions, model) live in the HAL-free
// board_config.h, included here so anything using the HAL still sees the full
// set it always did.
#include "board_config.h"

// --- MTU hardware-timer channel indices -------------------------------------
#define TIMER_MIDI_GATE_OUTPUT 2 // MIDI/gate clock
#define TIMER_SYSTEM_FAST      0 // "fast" events, 528 ticks/ms (528 kHz)
#define TIMER_SYSTEM_SLOW      4 // "slow" events, 32 ticks/ms (32 kHz)
#define TIMER_SYSTEM_SUPERFAST 1 // "superfast" events, 33.792 ticks/us (33.792 MHz)

// --- DMA request / level register values ------------------------------------
#define DMA_LVL_FOR_SSI   (1 << 6)
#define DMARS_FOR_SSI0_TX 0x00E1
#define DMARS_FOR_SSI0_RX 0x00E2

#define DMA_AM_FOR_SCIF    (0b010 << 8)
#define DMARS_FOR_SCIF0_RX 0b001100010
#define DMARS_FOR_SCIF0_TX 0b001100001

#define DMARS_FOR_RSPI_TX 0b100100001

// --- UART transport items ---------------------------------------------------
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

// --- Bus / port channels ----------------------------------------------------
#define SPI_CHANNEL_CV                 0
#define SPI_CHANNEL_OLED_MAIN          0
#define UART_CHANNEL_MIDI              0
#define UART_CHANNEL_PIC               1
#define UART_INITIAL_SPEED_PIC_PADS_HZ 31250

#define SPIBSC_CH   0
#define SSI_CHANNEL 0
#define SD_PORT     1

// --- DMA channel assignments ------------------------------------------------
/*
DMA channels:
0:
1:
2: SD host - defined in sd_cfg.h
3:
4: OLED and CV SPI (if OLED supported)
5:
6: audio TX
7: audio RX
8: SD SPI TX for 40-pad Deluge - defined in mmc.c
9: SD SPI RX for 40-pad Deluge
10: PIC UART TX
11: MIDI UART TX
12: PIC UART RX
13: MIDI UART RX
14: MIDI UART RX timing
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

// --- Memory map -------------------------------------------------------------
#define EXTERNAL_MEMORY_BEGIN       0x0C000000
#define EXTERNAL_MEMORY_END         0x10000000
#define UNUSED_MEMORY_SPACE_ADDRESS 0x08000000

#define INTERNAL_MEMORY_BEGIN 0x20000000uL

#define UNCACHED_MIRROR_OFFSET 0x40000000

#ifndef HAVE_RTT
#define HAVE_RTT 0
#endif

#endif /* SYSTEM_CPU_SPECIFIC_H_ */
