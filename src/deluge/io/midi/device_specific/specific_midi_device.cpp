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

void midiDeviceCallHook(MIDIDeviceUSBHosted* device, SpecificMidiDeviceHook hook) {
	MIDIDeviceUSBHosted* specificDevice;

	if (MIDIDeviceLumiKeys::matchesVendorProduct(device->vendorId, device->productId)) {
		specificDevice = (MIDIDeviceLumiKeys*)device;
	}
	else {
		specificDevice = device;
	}

	switch (hook) {
	case SpecificMidiDeviceHook::ON_CONNECTED:
		specificDevice->hookOnConnected();
		break;
	case SpecificMidiDeviceHook::ON_CHANGE_KEY_OR_SCALE:
		specificDevice->hookOnChangeKeyOrScale();
		break;
	default:
		break;
	}
}
