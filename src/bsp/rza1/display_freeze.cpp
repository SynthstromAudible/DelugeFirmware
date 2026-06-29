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

/// RZ/A1L implementation of deluge_display_freeze() — the fault-handler's
/// synchronous OLED blit. The body is relocated verbatim from the old
/// OLED::freezeWithError(): it quiesces any in-flight transfer, drives the PIC
/// OLED select handshake, configures RSPI + DMAC manually and blits the frame,
/// then blocks until the user presses the select knob (PIC code 175) to resume.
///
/// Uses the PIC class (which pulls application types), so like control_surface.cpp
/// this compiles into the `deluge` target rather than the bsp_rza1 library.

#include "libdeluge/display.h"

#include "definitions.h"     // OLED_MAIN_WIDTH/HEIGHT_PIXELS
#include "drivers/pic/pic.h" // PIC (C++ class; pulls its C deps in extern "C")

// The remaining headers are C; wrap them so their functions keep C linkage.
extern "C" {
#include "RZA1/cache/cache.h"         // invalidate_range_all_caches
#include "RZA1/cpu_specific.h"        // SPI_CHANNEL_OLED_MAIN, OLED_SPI_DMA_CHANNEL, UART_ITEM_*, TIMER_SYSTEM_SLOW
#include "RZA1/mtu/mtu.h"             // TCNT
#include "RZA1/oled/oled_low_level.h" // oledWaitingForMessage
#include "RZA1/rspi/rspi.h"           // RSPI()
#include "RZA1/uart/sio_char.h"       // uartGetChar, uartFlushIfNotSending
#include "drivers/dmac/dmac.h"        // DMACn, DMAC0_CHSTAT_n_TC, DMAC_CHCTRL_0S_*
#include "drivers/oled/oled.h"        // spiBusCurrentlySending
}

// ms * 33 == counts of TIMER_SYSTEM_SLOW per millisecond (was cfunctions::msToSlowTimerCount).
#define SLOW_TIMER_COUNT_FOR_MS(ms) ((ms) * 33)

void deluge_display_freeze(const uint8_t* pixels) {
	// Wait for existing DMA transfer to finish
	uint16_t startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while (!(DMACn(OLED_SPI_DMA_CHANNEL).CHSTAT_n & DMAC0_CHSTAT_n_TC)
	       && (uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < SLOW_TIMER_COUNT_FOR_MS(50)) {}

	// Wait for PIC to de-select OLED, if it's been doing that.
	if (oledWaitingForMessage != 256) {
		startTime = *TCNT[TIMER_SYSTEM_SLOW];
		while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < SLOW_TIMER_COUNT_FOR_MS(50)) {
			uint8_t value;
			bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
			if (anything && value == oledWaitingForMessage) {
				break;
			}
		}
		oledWaitingForMessage = 256;
	}
	spiBusCurrentlySending = false;

	// Select OLED
	PIC::selectOLED();
	PIC::flush();
	oledWaitingForMessage = 248;

	// Wait for selection to be done
	startTime = *TCNT[TIMER_SYSTEM_SLOW];
	while ((uint16_t)(*TCNT[TIMER_SYSTEM_SLOW] - startTime) < SLOW_TIMER_COUNT_FOR_MS(50)) {
		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything && value == 248) {
			break;
		}
	}
	oledWaitingForMessage = 256;

	// Send data via DMA
	RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR = 0x20u;               // 8-bit
	RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0 = 0b0000011100000010; // 8-bit
	RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;    // 0b00100000;

	int32_t transferSize = (OLED_MAIN_HEIGHT_PIXELS >> 3) * OLED_MAIN_WIDTH_PIXELS;
	DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n = transferSize; // TODO: only do this once?
	uint32_t dataAddress = (uint32_t)pixels;
	DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n = dataAddress;
	// todo - should only need a flush
	invalidate_range_all_caches(dataAddress, dataAddress + transferSize);
	DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
	    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----

	while (1) {
		PIC::flush();
		uartFlushIfNotSending(UART_ITEM_MIDI);

		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything) {
			if (value == 175) {
				break;
			}
			else if (value == 249) {}
		}
	}
	oledWaitingForMessage = 256;
	spiBusCurrentlySending = false;
}
