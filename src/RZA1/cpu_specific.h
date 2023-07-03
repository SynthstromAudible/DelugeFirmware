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

#define NUM_CV_CHANNELS   2
#define NUM_GATE_CHANNELS 4

#define DMA_LVL_FOR_SSI   (1 << 6)
#define DMARS_FOR_SSI0_TX 0x00E1
#define DMARS_FOR_SSI0_RX 0x00E2

#define DMA_AM_FOR_SCIF    (0b010 << 8)
#define DMARS_FOR_SCIF0_RX 0b001100010
#define DMARS_FOR_SCIF0_TX 0b001100001

#define DMARS_FOR_RSPI_TX 0b100100001

// UART ----------------------------------------------------
#define UART_ITEM_PIC  0
#define UART_ITEM_MIDI 1
#define NUM_UART_ITEMS 2

// Aliases, cos multiple things on same PIC for this device
#define UART_ITEM_PIC_PADS       UART_ITEM_PIC
#define UART_ITEM_PIC_INDICATORS UART_ITEM_PIC

// PIC UART buffer sizes - different here on A1 because on A2 we have 3 of them
#define PIC_TX_BUFFER_SIZE                                                                                             \
    1024 // Theoretically I should be able to do just 512, and it almost works, but occasionally the cursor pos won't update - at the end of an audio clip record
#define PIC_RX_BUFFER_SIZE 64

#define TIMING_CAPTURE_ITEM_MIDI 0
#define NUM_TIMING_CAPTURE_ITEMS 1

#define UART_FULL_SPEED_PIC_PADS_HZ 200000 // 400000 glitches sometimes, especially if you zoom lots

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
#define NUM_BUTTON_ROWS             3
#define NUM_BUTTON_COLS             10
#define NUM_LED_COLS                10
#define SPI_CHANNEL_CV              1
#define UART_CHANNEL_MIDI           1
#define UART_CHANNEL_PIC            2
#define UI_MS_PER_REFRESH           33
#define UI_MS_PER_REFRESH_SCROLLING 12
#else
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
    7 // Any faster than this is faster than the UART bus can do, so there's a lag, and if we're sending out multiple scrolls fast, the buffer can build up quite a lag
#endif

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

// Buttons / LEDs ---------------------------------------------------------------

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD

#define shiftButtonX 9
#define shiftButtonY 0

#define playButtonX 9
#define playButtonY 2
#define playLedX    9
#define playLedY    1

#define recordButtonX 9
#define recordButtonY 1
#define recordLedX    9
#define recordLedY    2

#define clipViewButtonX 3
#define clipViewButtonY 0
#define clipViewLedX    3
#define clipViewLedY    3

#define sessionViewButtonX 2
#define sessionViewButtonY 0
#define sessionViewLedX    2
#define sessionViewLedY    3

#define synthButtonX 5
#define synthButtonY 1
#define synthLedX    5
#define synthLedY    2

#define kitButtonX 5
#define kitButtonY 2
#define kitLedX    5
#define kitLedY    1

#define midiButtonX 6
#define midiButtonY 2
#define midiLedX    6
#define midiLedY    1

#define cvButtonX 6
#define cvButtonY 1
#define cvLedX    6
#define cvLedY    2

#define learnButtonX 6
#define learnButtonY 0
#define learnLedX    6
#define learnLedY    3

#define tapTempoButtonX 8
#define tapTempoButtonY 2
#define tapTempoLedX    8
#define tapTempoLedY    1

#define saveButtonX 7
#define saveButtonY 0
#define saveLedX    7
#define saveLedY    3

#define loadButtonX 7
#define loadButtonY 1
#define loadLedX    7
#define loadLedY    2

#define scaleModeButtonX 4
#define scaleModeButtonY 0
#define scaleModeLedX    4
#define scaleModeLedY    3

#define tripletsButtonX 8
#define tripletsButtonY 0
#define tripletsLedX    8
#define tripletsLedY    3

#else

#define shiftButtonX 8
#define shiftButtonY 0
#define shiftLedX    8
#define shiftLedY    0

#define playButtonX 8
#define playButtonY 3
#define playLedX    8
#define playLedY    3

#define recordButtonX 8
#define recordButtonY 2
#define recordLedX    8
#define recordLedY    2

#define clipViewButtonX 3
#define clipViewButtonY 2
#define clipViewLedX    3
#define clipViewLedY    2

#define sessionViewButtonX 3
#define sessionViewButtonY 1
#define sessionViewLedX    3
#define sessionViewLedY    1

#define synthButtonX 5
#define synthButtonY 0
#define synthLedX    5
#define synthLedY    0

#define kitButtonX 5
#define kitButtonY 1
#define kitLedX    5
#define kitLedY    1

#define midiButtonX 5
#define midiButtonY 2
#define midiLedX    5
#define midiLedY    2

#define cvButtonX 5
#define cvButtonY 3
#define cvLedX    5
#define cvLedY    3

#define learnButtonX 7
#define learnButtonY 0
#define learnLedX    7
#define learnLedY    0

#define tapTempoButtonX 7
#define tapTempoButtonY 3
#define tapTempoLedX    7
#define tapTempoLedY    3

#define saveButtonX 6
#define saveButtonY 3
#define saveLedX    6
#define saveLedY    3

#define loadButtonX 6
#define loadButtonY 1
#define loadLedX    6
#define loadLedY    1

#define scaleModeButtonX 6
#define scaleModeButtonY 0
#define scaleModeLedX    6
#define scaleModeLedY    0

#define keyboardButtonX 3
#define keyboardButtonY 3
#define keyboardLedX    3
#define keyboardLedY    3

#define selectEncButtonX 4
#define selectEncButtonY 3

#define backButtonX 7
#define backButtonY 1
#define backLedX    7
#define backLedY    1

#define tripletsButtonX 8
#define tripletsButtonY 1
#define tripletsLedX    8
#define tripletsLedY    1

#define BATTERY_LED_1      1
#define BATTERY_LED_2      1
#define SYS_VOLT_SENSE_PIN 5

// Moving this to preprocessor build directives
// eventually will be distinguished by build script flags
// Change your build to change whether you're building OLED or not
#ifndef HAVE_OLED
#define HAVE_OLED 0
#endif
#ifndef HAVE_RTT
#define HAVE_RTT 0
#endif

#define NUM_ENCODERS          6
#define NUM_FUNCTION_ENCODERS 4
#define ENCODER_SCROLL_X      1
#define ENCODER_SCROLL_Y      0
#define ENCODER_TEMPO         3
#define ENCODER_SELECT        2
#define ENCODER_MOD_0         5
#define ENCODER_MOD_1         4

#define SYNCED_LED_PORT         6
#define SYNCED_LED_PIN          7

// OLED
// --- 64 pixels high
//#define OLED_MAIN_HEIGHT_PIXELS 64
//#define OLED_MAIN_TOPMOST_PIXEL 2
//#define OLED_HEIGHT_CHARS 6

// --- 48 pixels high
#define OLED_MAIN_HEIGHT_PIXELS 48
#define OLED_MAIN_TOPMOST_PIXEL 5
#define OLED_HEIGHT_CHARS       4

#define OLED_MAIN_VISIBLE_HEIGHT (OLED_MAIN_HEIGHT_PIXELS - OLED_MAIN_TOPMOST_PIXEL)

#endif

#endif /* SYSTEM_CPU_SPECIFIC_H_ */
