/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#pragma once

#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_routing.h"
#include "storage/storage_manager.h"
#include "util/containers.h"
#include "util/d_string.h"
#include <cstring>
#include <string_view>

namespace deluge::io::midi {

inline MIDICable* getCableForOutputIndex(uint8_t deviceIndex) {
	if (deviceIndex <= 1) {
		return deviceIndex == 1 ? &MIDIDeviceManager::dinMIDIPorts : nullptr;
	}
	if (deviceIndex == 2) {
		return &MIDIDeviceManager::upstreamUSBMIDICable1;
	}
	if (deviceIndex == 3) {
		return &MIDIDeviceManager::upstreamUSBMIDICable2;
	}
	int32_t hostedIndex = static_cast<int32_t>(deviceIndex) - 4;
	if (hostedIndex >= 0 && hostedIndex < MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		return static_cast<MIDICable*>(MIDIDeviceManager::hostedMIDIDevices.getElement(hostedIndex));
	}
	return nullptr;
}

inline std::string_view getDeviceNameForIndex(uint8_t deviceIndex) {
	if (deviceIndex == 0) {
		return "ALL";
	}
	MIDICable* cable = getCableForOutputIndex(deviceIndex);
	if (cable != nullptr) {
		char const* name = cable->getDisplayName();
		return name != nullptr ? std::string_view(name) : std::string_view{};
	}
	return {};
}

// Routing index + label for the Output Device menu.
// 0 = ALL, 1 = DIN, 2/3 = upstream USB (computer), 4+ = hosted USB MIDI.
struct MIDIOutputDeviceOption {
	uint8_t index;
	std::string_view name;
};

inline bool isMIDIOutputDeviceConnected(uint8_t deviceIndex) {
	if (deviceIndex <= 1) {
		return true; // ALL and DIN are always available
	}
	MIDICable* cable = getCableForOutputIndex(deviceIndex);
	return cable != nullptr && cable->connectionFlags != 0;
}

// Connected destinations only. Upstream USB ports appear when the Deluge is plugged into a
// computer; hosted devices appear when they are attached. alsoIncludeIndex keeps a saved
// selection visible if that destination is temporarily unplugged.
inline deluge::vector<MIDIOutputDeviceOption> getVisibleMIDIOutputDevices(uint8_t alsoIncludeIndex = 255) {
	deluge::vector<MIDIOutputDeviceOption> options;

	auto maybeAdd = [&](uint8_t index) {
		if (index != alsoIncludeIndex && !isMIDIOutputDeviceConnected(index)) {
			return;
		}
		std::string_view name = getDeviceNameForIndex(index);
		if (!name.empty()) {
			options.push_back({index, name});
		}
	};

	maybeAdd(0);
	maybeAdd(1);
	maybeAdd(2);
	maybeAdd(3);
	for (int32_t i = 0; i < MIDIDeviceManager::hostedMIDIDevices.getNumElements(); i++) {
		maybeAdd(static_cast<uint8_t>(i + 4));
	}
	return options;
}

inline uint8_t deviceIndexToMenuSlot(uint8_t deviceIndex, uint8_t alsoIncludeIndex = 255) {
	auto options = getVisibleMIDIOutputDevices(alsoIncludeIndex);
	for (size_t i = 0; i < options.size(); i++) {
		if (options[i].index == deviceIndex) {
			return static_cast<uint8_t>(i);
		}
	}
	return 0;
}

inline uint8_t menuSlotToDeviceIndex(uint8_t slot, uint8_t alsoIncludeIndex = 255) {
	auto options = getVisibleMIDIOutputDevices(alsoIncludeIndex);
	if (slot >= options.size()) {
		return 0;
	}
	return options[slot].index;
}

inline uint8_t findDeviceIndexByName(std::string_view deviceName, uint8_t fallbackIndex = 0) {
	if (deviceName.empty() || deviceName == "ALL") {
		return deviceName == "ALL" ? 0 : fallbackIndex;
	}

	if (deviceName == MIDIDeviceManager::dinMIDIPorts.getDisplayName()) {
		return 1;
	}
	if (deviceName == MIDIDeviceManager::upstreamUSBMIDICable1.getDisplayName()) {
		return 2;
	}
	if (deviceName == MIDIDeviceManager::upstreamUSBMIDICable2.getDisplayName()) {
		return 3;
	}

	for (int32_t i = 0; i < MIDIDeviceManager::hostedMIDIDevices.getNumElements(); i++) {
		auto* cable = static_cast<MIDICable*>(MIDIDeviceManager::hostedMIDIDevices.getElement(i));
		if (cable != nullptr && deviceName == cable->getDisplayName()) {
			return static_cast<uint8_t>(i + 4);
		}
	}

	return fallbackIndex;
}

inline void writeDeviceToFile(Serializer& writer, uint8_t deviceIndex, String& deviceName,
                              const char* attributeName = "outputDevice") {
	if (deviceIndex == 0) {
		return;
	}
	writer.writeAttribute(attributeName, deviceIndex, false);
	if (!deviceName.isEmpty()) {
		char nameAttr[64];
		snprintf(nameAttr, sizeof(nameAttr), "%sName", attributeName);
		writer.writeAttribute(nameAttr, deviceName.get(), false);
	}
}

inline void readDeviceFromAttributes(Deserializer& reader, uint8_t& outDeviceIndex, String& outDeviceName,
                                     const char* deviceAttrName, const char* nameAttrName) {
	uint8_t savedIndex = 0;
	String savedName;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, deviceAttrName)) {
			savedIndex = static_cast<uint8_t>(reader.readTagOrAttributeValueInt());
		}
		else if (!strcmp(tagName, nameAttrName)) {
			reader.readTagOrAttributeValueString(&savedName);
		}
		reader.exitTag();
	}

	if (!savedName.isEmpty()) {
		outDeviceIndex = findDeviceIndexByName(savedName.get(), savedIndex);
		outDeviceName.set(&savedName);
	}
	else {
		outDeviceIndex = savedIndex;
	}
}

inline deluge::vector<std::string_view> getAllMIDIDeviceNames(uint8_t alsoIncludeIndex = 255) {
	deluge::vector<std::string_view> names;
	for (auto const& option : getVisibleMIDIOutputDevices(alsoIncludeIndex)) {
		names.push_back(option.name);
	}
	return names;
}

} // namespace deluge::io::midi
