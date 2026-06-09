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

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h" // usb_utr_t, USB_NUM_USBIP
#include "board_config.h"                        // MAX_NUM_USB_MIDI_DEVICES, PLACE_SDRAM_BSS

#include <string.h>

// Phase 3: the transport *state* now lives here (relocated from the application's
// ConnectedUSBMIDIDevice and from midi_engine.cpp). The transport *functions* (the
// send/receive state machine) still live in midi_engine.cpp and reach in through
// bsp_usb_midi_device(); they migrate here in phases 4-5, at which point the
// service/read/write stubs below become real.

// Per-device USB-MIDI transport state. The receive DMA targets receiveData
// directly, so this stays in SDRAM (as it did inside connectedUSBMIDIDevices).
PLACE_SDRAM_BSS BspUsbMidiDevice g_usb_midi_devices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

// Renesas USB transfer descriptors for the MIDI bulk pipes, moved here from
// midi_engine.cpp. The HAL and the application reference them via extern; their
// fields (callbacks, ip/ipp, DMA pointers) are still initialised in midi_engine.
usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

BspUsbMidiDevice* bsp_usb_midi_device(uint8_t ip, uint8_t d) {
	return &g_usb_midi_devices[ip][d];
}

void bsp_usb_midi_init(void) {
	memset(g_usb_midi_devices, 0, sizeof g_usb_midi_devices);
}

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
