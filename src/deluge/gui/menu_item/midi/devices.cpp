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
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include <array>

extern deluge::gui::menu_item::Submenu<2> midiDeviceMenu;

namespace deluge::gui::menu_item::midi {

static const int32_t lowestDeviceNum = -3;

void Devices::beginSession(MenuItem* navigatedBackwardFrom) {
	if (navigatedBackwardFrom != nullptr) {
		for (this->value_ = lowestDeviceNum; this->value_ < MIDIDeviceManager::hostedMIDIDevices.getNumElements();
		     this->value_++) {
			if (getDevice(this->value_) == soundEditor.currentMIDIDevice) {
				goto decidedDevice;
			}
		}
	}

	this->value_ = lowestDeviceNum; // Start on "DIN". That's the only one that'll always be there.

decidedDevice:
	soundEditor.currentMIDIDevice = getDevice(this->value_);
	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = this->value_;
	}
	else {
		drawValue();
	}
}

void Devices::selectEncoderAction(int32_t offset) {
	do {
		int32_t newValue = this->value_ + offset;

		if (newValue >= MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
			if (display.type == DisplayType::OLED) {
				return;
			}
			newValue = lowestDeviceNum;
		}
		else if (newValue < lowestDeviceNum) {
			if (display.type == DisplayType::OLED) {
				return;
			}
			newValue = MIDIDeviceManager::hostedMIDIDevices.getNumElements() - 1;
		}

		this->value_ = newValue;

		soundEditor.currentMIDIDevice = getDevice(this->value_);

	} while (!soundEditor.currentMIDIDevice->connectionFlags);
	// Don't show devices which aren't connected. Sometimes we won't even have a name to display for them.

	if (display.type == DisplayType::OLED) {
		if (this->value_ < soundEditor.menuCurrentScroll) {
			soundEditor.menuCurrentScroll = this->value_;
		}

		if (offset >= 0) {
			int32_t d = this->value_;
			int32_t numSeen = 1;
			while (true) {
				d--;
				if (d == soundEditor.menuCurrentScroll) {
					break;
				}
				if (!getDevice(d)->connectionFlags) {
					continue;
				}
				numSeen++;
				if (numSeen >= kOLEDMenuNumOptionsVisible) {
					soundEditor.menuCurrentScroll = d;
					break;
				}
			}
		}
	}

	drawValue();
}

MIDIDevice* Devices::getDevice(int32_t deviceIndex) {
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
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		char const* displayName = soundEditor.currentMIDIDevice->getDisplayName();
		display.setScrollingText(displayName);
	}
}

MenuItem* Devices::selectButtonPress() {
	if (display.type == DisplayType::OLED) {
		// A bit ugly, but saves us extending a class.
		midiDeviceMenu.title = soundEditor.currentMIDIDevice->getDisplayName();
	}
	return &midiDeviceMenu;
}

void Devices::drawPixelsForOled() {
	static_vector<string, kOLEDMenuNumOptionsVisible> itemNames = {};

	int32_t selectedRow = -1;

	int32_t device_idx = soundEditor.menuCurrentScroll;
	size_t row = 0;
	while (row < kOLEDMenuNumOptionsVisible && device_idx < MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		MIDIDevice* device = getDevice(device_idx);
		if (device->connectionFlags != 0u) {
			itemNames[row] = device->getDisplayName();
			if (device_idx == this->value_) {
				selectedRow = static_cast<int32_t>(row);
			}
			row++;
		}
		device_idx++;
	}

	drawItemsForOled(itemNames, selectedRow, soundEditor.menuCurrentScroll);
}

} // namespace deluge::gui::menu_item::midi
