/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// RZ/A1L implementation of the board bring-up entry points in <libdeluge/board.h>.
///
/// Part of bsp-rza1-legacy. Holds the one-time hardware bring-up lifted out of
/// the application's boot path (deluge.cpp): GPIO direction/mux, the CV DAC SPI,
/// the OLED-shared SPI plumbing, the audio SSI, the serial-flash controller and
/// the L2 cache unlock. The board owns every pin/port number, SPI channel and
/// register address; none of that crosses the boundary. The {port,pin} numbers
/// below mirror the `constexpr Pin` table in definitions_cxx.hpp, of which this
/// file (with signals.c) is now the authoritative hardware copy.
///
///   BATTERY_LED {1,1}  SYNCED_LED {6,7}  CODEC {6,12}  SPEAKER_ENABLE {4,1}
///   HEADPHONE_DETECT {6,5}  LINE_IN_DETECT {6,6}  MIC_DETECT {7,9}
///   LINE_OUT_DETECT_L {6,3}  LINE_OUT_DETECT_R {6,4}  ANALOG_CLOCK_IN {1,14}
///   VOLT_SENSE {1,13}  SPI_CLK {6,0}  SPI_MOSI {6,2}  SPI_SSL {6,1}

#include "libdeluge/board.h"

#include "RZA1/cache/cache.h"                // L2CacheUnlockData
#include "RZA1/cpu_specific.h"               // SPI_CHANNEL_CV, OLED_SPI_DMA_CHANNEL, DMACn
#include "RZA1/gpio/gpio.h"                  // setPinMux/setPinAsInput/setPinAsOutput/setOutputState
#include "RZA1/oled/oled_low_level.h"        // setupSPIInterrupts
#include "RZA1/rspi/rspi.h"                  // R_RSPI_Create/Start
#include "RZA1/spibsc/spibsc_Deluge_setup.h" // initSPIBSC
#include "drivers/oled/oled.h"               // oledDMAInit
#include "drivers/ssi/ssi.h"                 // ssiInit

bool deluge_board_probe_oled(void) {
	// Piggyback off of the bootloader's DMA setup: if the OLED SPI DMA channel is
	// configured the way the bootloader leaves it, an OLED panel is fitted.
	uint32_t oledSPIDMAConfig = (0b1101000 | (OLED_SPI_DMA_CHANNEL & 7));
	return ((DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n & oledSPIDMAConfig) == oledSPIDMAConfig);
}

void deluge_board_init_early(bool have_oled) {
	// Status LEDs / audio-path enables: direction + initial (off) state.
	setOutputState(1, 1, 1); // BATTERY_LED off (open-drain: 1 is off)
	setPinAsOutput(1, 1);

	setOutputState(6, 7, 0); // SYNCED_LED off
	setPinAsOutput(6, 7);

	setPinAsOutput(6, 12);    // Codec control
	setOutputState(6, 12, 0); // off

	setPinAsOutput(4, 1);    // Speaker / amp control
	setOutputState(4, 1, 0); // off

	setPinAsInput(6, 5); // Headphone detect
	setPinAsInput(6, 6); // Line in detect
	setPinAsInput(7, 9); // Mic detect

	setPinMux(1, 13, 1); // Analog input for voltage sense (VOLT_SENSE)
	setPinMux(1, 14, 2); // Trigger clock input (ANALOG_CLOCK_IN)

	setPinAsInput(6, 3); // Line out detect L
	setPinAsInput(6, 4); // Line out detect R

	// SPI for CV. Higher than 10 MHz would probably work, but when the OLED shares
	// this channel we stick to the OLED datasheet's 100 ns (10 MHz) spec.
	R_RSPI_Create(SPI_CHANNEL_CV, have_oled ? 10000000 : 30000000, 0, 32);
	R_RSPI_Start(SPI_CHANNEL_CV);
	setPinMux(6, 0, 3); // SPI_CLK
	setPinMux(6, 2, 3); // SPI_MOSI

	if (have_oled) {
		// If the OLED shares the SPI channel we have to manually control the SSL pin.
		setOutputState(6, 1, 1); // SPI_SSL high
		setPinAsOutput(6, 1);
		setupSPIInterrupts();
		oledDMAInit();
	}
	else {
		setPinMux(6, 1, 3); // SPI_SSL
	}
}

void deluge_board_init_audio(void) {
	// Setup audio output on SSI0.
	ssiInit(0, 1);
}

void deluge_board_init_storage(void) {
	// Crucial that this only be done once everything else is running, because the
	// graphics and audio routines are injected into the SPIBSC wait routines, so
	// those have to be running (and ideally external RAM set up) by now.
	setPinMux(4, 2, 2);
	setPinMux(4, 3, 2);
	setPinMux(4, 4, 2);
	setPinMux(4, 5, 2);
	setPinMux(4, 6, 2);
	setPinMux(4, 7, 2);
	initSPIBSC(); // This will run the audio routine!
}

void deluge_board_unlock_data_cache(void) {
	L2CacheUnlockData();
}
