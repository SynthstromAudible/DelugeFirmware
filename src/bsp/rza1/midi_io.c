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

/// RZ/A1L implementation of <libdeluge/midi_io.h>.
///
/// Part of bsp-rza1-legacy; decoupled from application headers (only the UART
/// HAL and the BSP usb_midi transport), so it lives in the clean bsp_rza1
/// library.
///
/// Port indices on this board: the DIN serial port is DELUGE_MIDI_DIN (0);
/// ports 1..MAX_NUM_USB_MIDI_DEVICES carry USB-MIDI device slots 0.. on the
/// single USB controller (one port per device — its virtual cables are
/// addressed inside the 4-byte event packets that flow over the port).

#include "libdeluge/midi_io.h"

#include "RZA1/cpu_specific.h"       // UART_ITEM_MIDI
#include "RZA1/uart/sio_char.h"      // uartGetTxBufferSpace, uartFlushIfNotSending
#include "RZA1/usb/usb_host_event.h" // usbHostEventTake (HAL-owned USB host event queue)
#include "board_config.h"            // MAX_NUM_USB_MIDI_DEVICES
#include "usb_midi.h"                // bsp_usb_midi_* (USB-MIDI transport, BSP-internal)

#include <stddef.h> // NULL

// USB-MIDI ports follow the DIN port; single controller (ip 0) on this board.
#define USB_MIDI_PORT_BASE 1

static bool is_usb_port(DelugeMidiPort port) {
	return port >= USB_MIDI_PORT_BASE && port < USB_MIDI_PORT_BASE + MAX_NUM_USB_MIDI_DEVICES;
}

static uint8_t usb_device_of(DelugeMidiPort port) {
	return (uint8_t)(port - USB_MIDI_PORT_BASE);
}

void deluge_midi_init(void) {
	// DIN needs no init beyond the UART the board already brings up; the USB-MIDI
	// transport zeroes its state and prepares its transfer descriptors.
	bsp_usb_midi_init();
}

uint8_t deluge_midi_port_count(void) {
	return USB_MIDI_PORT_BASE + MAX_NUM_USB_MIDI_DEVICES;
}

DelugeMidiPortKind deluge_midi_port_kind(DelugeMidiPort port) {
	if (port == DELUGE_MIDI_DIN) {
		return DELUGE_MIDI_DIN;
	}
	return bsp_usb_midi_is_host() ? DELUGE_MIDI_USB_HOST : DELUGE_MIDI_USB_DEVICE;
}

DelugeMidiPort deluge_midi_usb_port(uint8_t controller, uint8_t device) {
	(void)controller; // Single USB controller on this board.
	return (DelugeMidiPort)(USB_MIDI_PORT_BASE + device);
}

void deluge_midi_service(void) {
	// DIN has no state machine; the USB-MIDI transport pumps its send/receive +
	// drains HAL completions here.
	bsp_usb_midi_service();
}

bool deluge_midi_port_connected(DelugeMidiPort port) {
	// The DIN serial port is always present; USB-MIDI ports only while enumerated.
	if (port == DELUGE_MIDI_DIN) {
		return true;
	}
	if (is_usb_port(port)) {
		return bsp_usb_midi_device_connected(0, usb_device_of(port));
	}
	return false;
}

bool deluge_midi_usb_is_host(void) {
	return bsp_usb_midi_is_host();
}

bool deluge_midi_usb_peripheral_connected(void) {
	return bsp_usb_midi_peripheral_connected();
}

DelugeUsbHostEvent deluge_midi_poll_usb_host_event(void) {
	switch (usbHostEventTake()) {
	case USB_HOST_EVENT_HUB_ATTACHED:
		return DELUGE_USB_HOST_HUB_ATTACHED;
	case USB_HOST_EVENT_DEVICE_DETACHED:
		return DELUGE_USB_HOST_DEVICE_DETACHED;
	case USB_HOST_EVENT_DEVICE_NOT_RECOGNIZED:
		return DELUGE_USB_HOST_DEVICE_NOT_RECOGNIZED;
	case USB_HOST_EVENT_DEVICES_MAX:
		return DELUGE_USB_HOST_DEVICES_MAX;
	default:
		return DELUGE_USB_HOST_NONE;
	}
}

uint32_t deluge_midi_read(DelugeMidiPort port, uint8_t* dst, uint32_t max) {
	return deluge_midi_read_timed(port, dst, max, NULL);
}

uint32_t deluge_midi_read_timed(DelugeMidiPort port, uint8_t* dst, uint32_t max, uint32_t* arrival_ticks) {
	if (is_usb_port(port)) {
		return bsp_usb_midi_read_timed(0, usb_device_of(port), dst, max, arrival_ticks);
	}
	// The DIN byte stream is read through deluge_midi_din_read_timed() (per-byte
	// hardware timestamps).
	return 0;
}

uint32_t deluge_midi_write(DelugeMidiPort port, const uint8_t* src, uint32_t len) {
	if (port == DELUGE_MIDI_DIN) {
		// Caller is expected to have checked deluge_midi_write_space() first, as
		// the original bufferMIDIUart() loop did.
		for (uint32_t i = 0; i < len; i++) {
			bufferMIDIUart(src[i]);
		}
		return len;
	}
	if (is_usb_port(port)) {
		return bsp_usb_midi_write(0, usb_device_of(port), src, len);
	}
	return 0;
}

uint32_t deluge_midi_write_space(DelugeMidiPort port) {
	if (port == DELUGE_MIDI_DIN) {
		return (uint32_t)uartGetTxBufferSpace(UART_ITEM_MIDI);
	}
	if (is_usb_port(port)) {
		return bsp_usb_midi_write_space(0, usb_device_of(port));
	}
	return 0;
}

uint32_t deluge_midi_write_pending(DelugeMidiPort port) {
	if (port == DELUGE_MIDI_DIN) {
		return (uint32_t)uartGetTxBufferFullnessByItem(UART_ITEM_MIDI);
	}
	if (is_usb_port(port)) {
		return bsp_usb_midi_write_pending(0, usb_device_of(port));
	}
	return 0;
}

bool deluge_midi_din_read_timed(uint8_t* byte, uint32_t* arrival_ticks) {
	// uartGetCharWithTiming returns a pointer to the byte's hardware arrival
	// timestamp (NULL when nothing is buffered). The slot id never crosses the
	// boundary: it is the board's DIN-MIDI capture slot, fixed here.
	uint32_t* timer = uartGetCharWithTiming(TIMING_CAPTURE_ITEM_MIDI, (char*)byte);
	if (!timer) {
		return false;
	}
	*arrival_ticks = *timer;
	return true;
}

void deluge_midi_flush(DelugeMidiPort port) {
	if (port == DELUGE_MIDI_DIN) {
		uartFlushIfNotSending(UART_ITEM_MIDI);
	}
	else if (is_usb_port(port)) {
		// USB-MIDI device slots share the controller's wire: flushing any USB
		// port pumps the whole USB-MIDI transport (as the boundary documents).
		bsp_usb_midi_flush_output();
	}
}
