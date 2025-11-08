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

#pragma once

#include "io/midi/midi_device_manager.h"
#include "util/containers.h"
#include "util/d_string.h"
#include <string_view>

namespace deluge::io::midi {

/// Get the device name for a given device index
/// @param deviceIndex 0=ALL, 1=DIN, 2+=USB devices
/// @return Device name as string_view, or empty if invalid
inline std::string_view getDeviceNameForIndex(uint8_t deviceIndex) {
	if (deviceIndex == 0) {
		return "ALL";
	}
	else if (deviceIndex == 1) {
		// Get DIN cable name
		return MIDIDeviceManager::root_din.cable.getDisplayName();
	}
	else {
		// USB device (index 2 = first USB, 3 = second USB, etc.)
		if (MIDIDeviceManager::root_usb != nullptr) {
			uint32_t usbIndex = deviceIndex - 2;
			size_t numCables = MIDIDeviceManager::root_usb->getNumCables();
			if (usbIndex < numCables) {
				MIDICable* cable = MIDIDeviceManager::root_usb->getCable(usbIndex);
				if (cable != nullptr) {
					return cable->getDisplayName();
				}
			}
		}
	}
	return {}; // Empty if not found
}

/// Find device index by matching device name
/// @param deviceName The device name to search for
/// @param fallbackIndex The index to use if name not found
/// @return Device index (0=ALL, 1=DIN, 2+=USB), or fallbackIndex if not found
inline uint8_t findDeviceIndexByName(std::string_view deviceName, uint8_t fallbackIndex = 0) {
	if (deviceName.empty()) {
		return fallbackIndex;
	}

	// Check for ALL
	if (deviceName == "ALL") {
		return 0;
	}

	// Check DIN
	if (deviceName == MIDIDeviceManager::root_din.cable.getDisplayName()) {
		return 1;
	}

	// Check USB devices
	if (MIDIDeviceManager::root_usb != nullptr) {
		size_t numCables = MIDIDeviceManager::root_usb->getNumCables();
		for (size_t i = 0; i < numCables; i++) {
			MIDICable* cable = MIDIDeviceManager::root_usb->getCable(i);
			if (cable != nullptr && deviceName == cable->getDisplayName()) {
				return 2 + i; // USB devices start at index 2
			}
		}
	}

	// Name not found - use fallback index
	return fallbackIndex;
}

/// Write device selection to file (saves both index and name)
/// @param writer The serializer to write to
/// @param deviceIndex The device index to save
/// @param deviceName The device name to save
/// @param attributeName The XML attribute name for the device index (default: "outputDevice")
inline void writeDeviceToFile(Serializer& writer, uint8_t deviceIndex, String& deviceName,
                              const char* attributeName = "outputDevice") {
	if (deviceIndex != 0) { // Don't save if ALL (default)
		writer.writeAttribute(attributeName, deviceIndex, false);
		if (!deviceName.isEmpty()) {
			// Use consistent attribute name by appending "Name"
			char nameAttr[64];
			snprintf(nameAttr, sizeof(nameAttr), "%sName", attributeName);
			writer.writeAttribute(nameAttr, deviceName.get(), false);
		}
	}
}

/// Read device selection from file (loads both index and name, matches by name)
/// @param reader The deserializer to read from
/// @param outDeviceIndex Reference to store the resolved device index
/// @param outDeviceName Reference to store the device name
/// @param deviceAttrName The XML attribute name for device index
/// @param nameAttrName The XML attribute name for device name
inline void readDeviceFromAttributes(Deserializer& reader, uint8_t& outDeviceIndex, String& outDeviceName,
                                     const char* deviceAttrName, const char* nameAttrName) {
	uint8_t savedIndex = 0;
	String savedName;

	const char* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, deviceAttrName)) {
			savedIndex = static_cast<uint8_t>(reader.readTagOrAttributeValueInt());
		}
		else if (!strcmp(tagName, nameAttrName)) {
			reader.readTagOrAttributeValueString(&savedName);
		}
		reader.exitTag();
	}

	// Match by name first (more reliable), fall back to index
	if (!savedName.isEmpty()) {
		outDeviceIndex = findDeviceIndexByName(savedName.get(), savedIndex);
		outDeviceName.set(&savedName);
	}
	else {
		outDeviceIndex = savedIndex;
	}
}

/// Get list of all available MIDI output devices
/// @return Vector of device names (ALL, DIN, USB devices...)
inline deluge::vector<std::string_view> getAllMIDIDeviceNames() {
	deluge::vector<std::string_view> options;

	// Always include ALL (0)
	options.push_back("ALL");

	// Always include DIN (1)
	options.push_back(MIDIDeviceManager::root_din.cable.getDisplayName());

	// Add USB devices using iterator
	if (MIDIDeviceManager::root_usb != nullptr) {
		for (auto& cable : MIDIDeviceManager::root_usb->getCables()) {
			options.push_back(cable.getDisplayName());
		}
	}

	return options;
}

} // namespace deluge::io::midi
