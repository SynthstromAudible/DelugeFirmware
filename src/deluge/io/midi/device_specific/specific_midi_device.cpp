/*
 * Copyright (c) 2023 Aria Burrell (litui)
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

#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_device_manager.h"

SpecificMidiDeviceType getSpecificMidiDeviceType(uint16_t vendorId, uint16_t productId) {
	if (MIDIDeviceLumiKeys::matchesVendorProduct(vendorId, productId)) {
		return SpecificMidiDeviceType::LUMI_KEYS;
	}

	return SpecificMidiDeviceType::NONE;
}

MIDIDeviceUSBHosted* recastSpecificMidiDevice(void* sourceDevice) {
	return recastSpecificMidiDevice((MIDIDeviceUSBHosted*)sourceDevice);
}

// This _should_ allow taking advantage of the overridden virtual functions on the children
MIDIDeviceUSBHosted* recastSpecificMidiDevice(MIDIDeviceUSBHosted* sourceDevice) {
	if (MIDIDeviceLumiKeys::matchesVendorProduct(sourceDevice->vendorId, sourceDevice->productId)) {
		MIDIDeviceLumiKeys* targetDevice = (MIDIDeviceLumiKeys*)sourceDevice;
		return targetDevice;
	}

	MIDIDeviceUSBHosted* targetDevice = sourceDevice;
	return targetDevice;
}

MIDIDeviceUSBHosted* getSpecificDeviceFromMIDIDevice(MIDIDevice* sourceDevice) {
	using namespace MIDIDeviceManager;
	// This depends on the MIDIDevice having been originally cast as something with a name
	const char* sourceName = sourceDevice->getDisplayName();

	if (sourceDevice->connectionFlags && sourceName) {
		for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
			MIDIDeviceUSBHosted* candidate = recastSpecificMidiDevice(hostedMIDIDevices.getElement(i));

			const char* candidateName = candidate->getDisplayName();

			if (sourceName == candidateName && candidate->connectionFlags == sourceDevice->connectionFlags) {
				return candidate;
			}
		}
	}

	return NULL;
}
