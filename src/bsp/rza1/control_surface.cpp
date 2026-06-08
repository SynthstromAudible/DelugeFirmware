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

/// RZ/A1L implementation of <libdeluge/control_surface.h>.
///
/// Part of bsp-rza1-legacy. The control surface is the Deluge's PIC co-processor
/// (pads/buttons/encoders/LEDs over UART); this file forwards the boundary's
/// logical operations to the PIC protocol class, which stays BSP-internal.
///
/// Implemented so far: the indicator/LED output path. The pad-colour, button
/// event, and flush operations declared in the header are added as their
/// application consumers are migrated off the PIC class directly.
///
/// NOTE: this file depends on application types (RGB, definitions_cxx) via the
/// PIC class, so it is compiled into the `deluge` target for now rather than the
/// bsp_rza1 library. It moves into bsp_rza1 once the PIC class is decoupled from
/// application headers.

#include "libdeluge/control_surface.h"
#include "RZA1/cpu_specific.h"

#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include <array>
#include <cstddef>

void deluge_control_set_led(uint8_t led, bool on) {
	if (on) {
		PIC::setLEDOn(led);
	}
	else {
		PIC::setLEDOff(led);
	}
}

void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count) {
	std::array<uint8_t, kNumGoldKnobIndicatorLEDs> ring{};
	for (size_t i = 0; i < kNumGoldKnobIndicatorLEDs && i < count; i++) {
		ring[i] = levels[i];
	}
	PIC::setGoldKnobIndicator(which != 0, ring);
}

uint32_t deluge_control_pad_output_space(void) {
	// uartGetTxBufferSpace / UART_ITEM_PIC_PADS come via the PIC transport header.
	return (uint32_t)uartGetTxBufferSpace(UART_ITEM_PIC_PADS);
}

void deluge_control_flush(void) {
	PIC::flush();
}
