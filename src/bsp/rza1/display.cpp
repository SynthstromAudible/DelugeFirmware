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

/// RZ/A1L implementation of <libdeluge/display.h> (bring-up + service hooks).
///
/// Part of bsp-rza1-legacy. The Deluge OLED is driven over the PIC (select/DC)
/// and RSPI DMA (framebuffer); these hooks bring up the panel and pump that
/// low-level state machine. It drives the PIC (BSP-internal, like
/// control_surface.cpp), so it is compiled as C++. The framebuffer blit /
/// 7-segment operations declared in the header are added as their consumers
/// migrate.

#include "libdeluge/display.h"

#include "RZA1/cpu_specific.h" // RSPI0
#include "libdeluge/clock.h"   // deluge_clock_delay_ms

#include "drivers/pic/pic.h" // PIC OLED select/DC ops

extern "C" {
#include "RZA1/oled/oled_low_level.h" // oledRoutine, oledLowLevelTimerCallback
#include "drivers/oled/oled.h"        // oledMainInit
}

void deluge_display_init(void) {
	// Configure the shared SPI bus for 8-bit OLED transfers.
	RSPI0.SPDCR = 0x20u;               // 8-bit
	RSPI0.SPCMD0 = 0b0000011100000010; // 8-bit
	RSPI0.SPBFCR.BYTE = 0b01100000;

	PIC::setDCLow();
	PIC::enableOLED();
	PIC::selectOLED();
	PIC::flush();

	deluge_clock_delay_ms(5);

	oledMainInit();

	PIC::deselectOLED();
	PIC::flush();
}

void deluge_display_service(void) {
	oledRoutine();
}

void deluge_display_timer_event(void) {
	oledLowLevelTimerCallback();
}
