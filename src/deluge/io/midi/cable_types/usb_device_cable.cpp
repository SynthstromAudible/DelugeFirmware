/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "usb_device_cable.h"
#include "class/midi/midi_device.h"
#include "gui/l10n/l10n.h"
#include "io/debug/log.h"
#include "storage/storage_manager.h"
#include <array>

void MIDICableUSBUpstream::writeReferenceAttributesToFile(Serializer& writer) {
	// Same line. Usually the user wouldn't have default velocity sensitivity set for their computer.
	writer.writeAttribute("port", portNumber ? "upstreamUSB2" : "upstreamUSB", false);
}

void MIDICableUSBUpstream::writeToFlash(uint8_t* memory) {
	D_PRINTLN("writing to flash port  %d  into ", portNumber);
	*(uint16_t*)memory = portNumber ? VENDOR_ID_UPSTREAM_USB2 : VENDOR_ID_UPSTREAM_USB;
}

char const* MIDICableUSBUpstream::getDisplayName() const {
	switch (portNumber) {
	case 0:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_1);
	case 1:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_2);
	case 2:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_3_SYSEX);
	default:
		return "";
	}
}

size_t MIDICableUSBUpstream::sendBufferSpace() const {
	return tud_midi_n_available(0, portNumber);
}


Error MIDICableUSBUpstream::sendMessage(MIDIMessage message) {
	if (connectionFlags == 0u) {
		return Error::NONE;
	}

	uint32_t packet = message.asUSBPacket(portNumber);

	if (tud_midi_n_packet_write(0, reinterpret_cast<uint8_t*>(&packet))) {
		return Error::NONE;
	}

	return Error::OUT_OF_BUFFER_SPACE;
}
