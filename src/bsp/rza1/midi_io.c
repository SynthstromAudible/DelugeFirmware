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
/// HAL), so it lives in the clean bsp_rza1 library. So far only the DIN serial
/// port's flow-control and flush are wired up — the byte read/write paths and
/// USB-MIDI ports are added as midi_engine.cpp migrates off the HAL directly.
///
/// Port indices: the DIN serial port is index DELUGE_MIDI_DIN (0) on this board.

#include "libdeluge/midi_io.h"

#include "RZA1/cpu_specific.h"       // UART_ITEM_MIDI
#include "RZA1/uart/sio_char.h"      // uartGetTxBufferSpace, uartFlushIfNotSending
#include "RZA1/usb/usb_host_event.h" // usbHostEventTake (HAL-owned USB host event queue)
#include "usb_midi.h"                // bsp_usb_midi_* (USB-MIDI transport, BSP-internal)

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
	return bsp_usb_midi_port_connected(port);
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

uint32_t deluge_midi_write(DelugeMidiPort port, const uint8_t* src, uint32_t len) {
	switch (port) {
	case DELUGE_MIDI_DIN:
		// Caller is expected to have checked deluge_midi_write_space() first, as
		// the original bufferMIDIUart() loop did.
		for (uint32_t i = 0; i < len; i++) {
			bufferMIDIUart(src[i]);
		}
		return len;
	default:
		return 0;
	}
}

uint32_t deluge_midi_write_space(DelugeMidiPort port) {
	switch (port) {
	case DELUGE_MIDI_DIN:
		return (uint32_t)uartGetTxBufferSpace(UART_ITEM_MIDI);
	default:
		return 0;
	}
}

void deluge_midi_flush(DelugeMidiPort port) {
	switch (port) {
	case DELUGE_MIDI_DIN:
		uartFlushIfNotSending(UART_ITEM_MIDI);
		break;
	default:
		break;
	}
}
