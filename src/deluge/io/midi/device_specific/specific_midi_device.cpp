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

/// @brief Space-saving function to call a specific Hosted USB MIDI device's hook from any entry point
/// @param hook
void iterateAndCallSpecificDeviceHook(MIDICableUSBHosted::Hook hook) {
	using namespace MIDIDeviceManager;

	for (int32_t i = 0; i < hostedMIDIDevices.getNumElements(); i++) {
		auto* specificDevice = static_cast<MIDICableUSBHosted*>(hostedMIDIDevices.getElement(i));

		specificDevice->callHook(hook);
	}
}
