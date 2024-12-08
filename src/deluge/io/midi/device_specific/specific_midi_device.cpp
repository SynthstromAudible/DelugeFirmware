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

MIDICableUSBHosted* recastSpecificMidiDevice(void* sourceDevice) {
	return recastSpecificMidiDevice((MIDICableUSBHosted*)sourceDevice);
}

/// @brief Recasts a MIDIDeviceUSBHosted pointer to a specific child device and back, to take advantage of virtual
/// functions
/// @param sourceDevice The known MIDIDeviceUSBHosted
/// @return A MIDIDeviceUSBHosted pointer, cast from Specific MIDI devices if found.
MIDICableUSBHosted* recastSpecificMidiDevice(MIDICableUSBHosted* sourceDevice) {
	if (MIDIDeviceLumiKeys::matchesVendorProduct(sourceDevice->vendorId, sourceDevice->productId)) {
		MIDIDeviceLumiKeys* targetDevice = (MIDIDeviceLumiKeys*)sourceDevice;
		return targetDevice;
	}

	MIDICableUSBHosted* targetDevice = sourceDevice;
	return targetDevice;
}

/// @brief When a MIDIDevice is known, this locates the matching MIDIDeviceUSBHosted based on connectionFlags
/// @param sourceDevice The known MIDIDevice
/// @return A MIDIDeviceUSBHosted pointer, cast from Specific MIDI devices if found.
MIDICableUSBHosted* getSpecificDeviceFromMIDICable(MIDICable& cable) {
	using namespace MIDIDeviceManager;
	// This depends on the MIDIDevice having been originally cast as something with a name
	const char* sourceName = cable.getDisplayName();

	if (cable.connectionFlags && sourceName) {
		for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
			MIDICableUSBHosted* candidate = recastSpecificMidiDevice(hostedMIDIDevices.getElement(i));

			const char* candidateName = candidate->getDisplayName();

			if (sourceName == candidateName && candidate->connectionFlags == cable.connectionFlags) {
				return candidate;
			}
		}
	}

	return nullptr;
}

/// @brief Space-saving function to call a specific Hosted USB MIDI device's hook from any entry point
/// @param hook
void iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook hook) {
	using namespace MIDIDeviceManager;

	for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
		MIDICableUSBHosted* specificDevice = recastSpecificMidiDevice(hostedMIDIDevices.getElement(i));

		specificDevice->callHook(hook);
	}
}
