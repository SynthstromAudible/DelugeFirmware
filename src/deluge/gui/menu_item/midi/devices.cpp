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
#include "definitions.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include <array>

extern deluge::gui::menu_item::Submenu<2> midiDeviceMenu;

namespace deluge::gui::menu_item::midi {

static const int lowestDeviceNum = -3;

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
#if HAVE_OLED
	soundEditor.menuCurrentScroll = this->value_;
#else
	drawValue();
#endif
}

void Devices::selectEncoderAction(int offset) {
	do {
		int newValue = this->value_ + offset;

		if (newValue >= MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
			if (HAVE_OLED) {
				return;
			}
			newValue = lowestDeviceNum;
		}
		else if (newValue < lowestDeviceNum) {
			if (HAVE_OLED) {
				return;
			}
			newValue = MIDIDeviceManager::hostedMIDIDevices.getNumElements() - 1;
		}

		this->value_ = newValue;

		soundEditor.currentMIDIDevice = getDevice(this->value_);

	} while (!soundEditor.currentMIDIDevice->connectionFlags);
	// Don't show devices which aren't connected. Sometimes we won't even have a name to display for them.

#if HAVE_OLED
	if (this->value_ < soundEditor.menuCurrentScroll) {
		soundEditor.menuCurrentScroll = this->value_;
	}

	if (offset >= 0) {
		int d = this->value_;
		int numSeen = 1;
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
#endif

	drawValue();
}

MIDIDevice* Devices::getDevice(int deviceIndex) {
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
#if HAVE_OLED
	renderUIsForOled();
#else
	char const* displayName = soundEditor.currentMIDIDevice->getDisplayName();
	numericDriver.setScrollingText(displayName);
#endif
}

MenuItem* Devices::selectButtonPress() {
#if HAVE_OLED
	midiDeviceMenu.title =
	    soundEditor.currentMIDIDevice->getDisplayName(); // A bit ugly, but saves us extending a class.
#endif
	return &midiDeviceMenu;
}

Sized<char const**> getOptions() {
}

#if HAVE_OLED

void Devices::drawPixelsForOled() {
	static_vector<string, kOLEDMenuNumOptionsVisible> itemNames = {};

	int selectedRow = -1;

	int device_idx = soundEditor.menuCurrentScroll;
	size_t row = 0;
	while (row < kOLEDMenuNumOptionsVisible && device_idx < MIDIDeviceManager::hostedMIDIDevices.getNumElements()) {
		MIDIDevice* device = getDevice(device_idx);
		if (device->connectionFlags != 0u) {
			itemNames[row] = device->getDisplayName();
			if (device_idx == this->value_) {
				selectedRow = static_cast<int>(row);
			}
			row++;
		}
		device_idx++;
	}

	drawItemsForOled(itemNames, selectedRow, soundEditor.menuCurrentScroll);
}

#endif
} // namespace deluge::gui::menu_item::midi
