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

/// RZ/A1L implementation of <libdeluge/display.h> (service hooks).
///
/// Part of bsp-rza1-legacy. The Deluge OLED is driven over the PIC (select/DC)
/// and RSPI DMA (framebuffer); these hooks pump that low-level state machine.
/// Fully decoupled from application headers — only the OLED HAL is needed — so
/// this lives in the clean bsp_rza1 library. The framebuffer blit / 7-segment
/// operations declared in the header are added as their consumers migrate.

#include "libdeluge/display.h"

#include "RZA1/oled/oled_low_level.h"

void deluge_display_service(void) {
	oledRoutine();
}

void deluge_display_timer_event(void) {
	oledLowLevelTimerCallback();
}
