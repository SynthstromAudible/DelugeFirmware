/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "devices.h"
#include "device.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include <etl/vector.h>
#include <string_view>

extern deluge::gui::menu_item::midi::Device midiDeviceMenu;

namespace deluge::gui::menu_item::midi {

static constexpr int32_t lowestDeviceNum = -3;

void Devices::beginSession(MenuItem* navigatedBackwardFrom) {
	bool found = false;
	if (navigatedBackwardFrom != nullptr) {
		for (int32_t idx = lowestDeviceNum; idx < MIDIDeviceManager::hostedMIDIDevices.getNumElements(); idx++) {
			if (getDevice(idx) == soundEditor.currentMIDIDevice) {
				found = true;
				this->setValue(idx);
				break;
			}
		}
	}

	if (!found) {
		this->setValue(lowestDeviceNum); // Start on "DIN". That's the only one that'll always be there.
	}

	soundEditor.currentMIDIDevice = getDevice(this->getValue());
	if (display->haveOLED()) {
		currentScroll = this->getValue();
	}
	else {
		drawValue();
	}
}

void Devices::selectEncoderAction(int32_t offset) {
	offset = std::clamp<int32_t>(offset, -1, 1);

	do {
		int32_t newValue = this->getValue() + offset;

		if (newValue >= MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
			if (display->haveOLED()) {
				return;
			}
			newValue = lowestDeviceNum;
		}
		else if (newValue < lowestDeviceNum) {
			if (display->haveOLED()) {
				return;
			}
			newValue = MIDIDeviceManager::hostedMIDIDevices.getNumElements() - 1;
		}

		this->setValue(newValue);

		soundEditor.currentMIDIDevice = getDevice(this->getValue());

	} while (!soundEditor.currentMIDIDevice->connectionFlags);
	// Don't show devices which aren't connected. Sometimes we won't even have a name to display for them.

	if (display->haveOLED()) {
		if (this->getValue() < currentScroll) {
			currentScroll = this->getValue();
		}
		//
		if (offset >= 0) {
			int32_t d = this->getValue();
			int32_t numSeen = 1;
			while (d > lowestDeviceNum) {
				d--;
				if (d == currentScroll) {
					break;
				}
				auto device = getDevice(d);
				if (!(device && device->connectionFlags)) {
					continue;
				}
				numSeen++;
				if (numSeen >= kOLEDMenuNumOptionsVisible) {
					currentScroll = d;
					break;
				}
			}
		}
	}

	drawValue();
}

MIDIDevice* Devices::getDevice(int32_t deviceIndex) {
	if (deviceIndex < lowestDeviceNum || deviceIndex >= MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		D_PRINTLN("impossible device request");
		return nullptr;
	}
	switch (deviceIndex) {
	case -3: {
		return &MIDIDeviceManager::dinMIDIPorts;
	}
	case -2: {
		return &MIDIDeviceManager::upstreamUSBMIDIDevice_port1;
	}
	case -1: {
		return &MIDIDeviceManager::upstreamUSBMIDIDevice_port2;
	}
	default: {
		return static_cast<MIDIDevice*>(MIDIDeviceManager::hostedMIDIDevices.getElement(deviceIndex));
	}
	}
}

void Devices::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		char const* displayName = soundEditor.currentMIDIDevice->getDisplayName();
		display->setScrollingText(displayName);
	}
}

MenuItem* Devices::selectButtonPress() {
	return &midiDeviceMenu;
}

void Devices::drawPixelsForOled() {
	etl::vector<std::string_view, kOLEDMenuNumOptionsVisible> itemNames = {};

	int32_t selectedRow = -1;

	int32_t device_idx = currentScroll;
	size_t row = 0;
	while (row < kOLEDMenuNumOptionsVisible && device_idx < MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		MIDIDevice* device = getDevice(device_idx);
		if (device && device->connectionFlags != 0u) {
			itemNames.push_back(device->getDisplayName());
			if (device_idx == this->getValue()) {
				selectedRow = static_cast<int32_t>(row);
			}
			row++;
		}
		device_idx++;
	}

	drawItemsForOled(itemNames, selectedRow);
}

} // namespace deluge::gui::menu_item::midi
