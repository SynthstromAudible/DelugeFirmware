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

inline deluge::vector<std::string_view> getAllMIDIDeviceNames() {
	deluge::vector<std::string_view> options;
	options.push_back("ALL");
	options.push_back(MIDIDeviceManager::dinMIDIPorts.getDisplayName());
	options.push_back(MIDIDeviceManager::upstreamUSBMIDICable1.getDisplayName());
	options.push_back(MIDIDeviceManager::upstreamUSBMIDICable2.getDisplayName());
	for (int32_t i = 0; i < MIDIDeviceManager::hostedMIDIDevices.getNumElements(); i++) {
		auto* cable = static_cast<MIDICable*>(MIDIDeviceManager::hostedMIDIDevices.getElement(i));
		if (cable != nullptr) {
			options.push_back(cable->getDisplayName());
		}
	}
	return options;
}

} // namespace deluge::io::midi
