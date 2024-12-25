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
#include "gui/l10n/l10n.h"
#include "io/debug/log.h"
#include "storage/storage_manager.h"

void MIDIDeviceUSBUpstream::writeReferenceAttributesToFile(Serializer& writer) {
	// Same line. Usually the user wouldn't have default velocity sensitivity set for their computer.
	writer.writeAttribute("port", (portNumber != 0u) ? "upstreamUSB2" : "upstreamUSB", false);
}

void MIDIDeviceUSBUpstream::writeToFlash(uint8_t* memory) {
	D_PRINTLN("writing to flash port  %d  into ", portNumber);
	*(uint16_t*)memory = (portNumber != 0u) ? VENDOR_ID_UPSTREAM_USB2 : VENDOR_ID_UPSTREAM_USB;
}

char const* MIDIDeviceUSBUpstream::getDisplayName() {
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
