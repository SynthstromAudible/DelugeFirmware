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

#include "usb_midi.h"

// Phase 2 scaffolding (no behaviour change): the transport state machine, the
// per-device RX/TX buffers + send ring, and the Renesas USB transfer descriptors
// migrate here from midi_engine.cpp and the HAL rohan_midi handlers in phases 3-5.
// Until then these are inert stubs and the legacy USB-MIDI path remains in effect.

void bsp_usb_midi_service(void) {
}

bool bsp_usb_midi_port_connected(uint8_t port) {
	(void)port;
	return false;
}

uint32_t bsp_usb_midi_read(uint8_t port, uint8_t* dst, uint32_t max) {
	(void)port;
	(void)dst;
	(void)max;
	return 0;
}

uint32_t bsp_usb_midi_write(uint8_t port, const uint8_t* src, uint32_t len) {
	(void)port;
	(void)src;
	(void)len;
	return 0;
}
