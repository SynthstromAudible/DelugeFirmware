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
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include <algorithm>
#include <etl/vector.h>
#include <string_view>

extern deluge::gui::menu_item::midi::Device midiDeviceMenu;

namespace deluge::gui::menu_item::midi {

static constexpr int32_t lowestDeviceNum = -3;

void Devices::beginSession(MenuItem* navigatedBackwardFrom) {
	bool found = false;
	if (navigatedBackwardFrom != nullptr) {
		for (int32_t idx = lowestDeviceNum; idx < static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size());
		     idx++) {
			if (getCable(idx) == soundEditor.currentMIDICable) {
				found = true;
				this->setValue(idx);
				break;
			}
		}
	}

	if (!found) {
		this->setValue(lowestDeviceNum); // Start on "DIN". That's the only one that'll always be there.
	}

	soundEditor.currentMIDICable = getCable(this->getValue());
	if (display->haveOLED()) {
		current_scroll_ = computeScrollForSelected(this->getValue());
	}
	else {
		drawValue();
	}
}

int32_t Devices::computeScrollForSelected(int32_t selected) {
	// Walk upward from the selection, counting connected devices, until the viewport is full or we reach the
	// first device. This keeps the selection at (or above) the bottom row while showing as many devices above it
	// as fit, rather than scrolling the selection to the top and hiding everything above it.
	int32_t scroll = selected;
	int32_t numSeen = 1; // The selected device itself.
	int32_t d = selected;
	while (d > lowestDeviceNum && numSeen < kOLEDMenuNumOptionsVisible) {
		d--;
		MIDICable* cable = getCable(d);
		if (!(cable && cable->connectionFlags)) {
			continue; // Disconnected devices aren't drawn, so they don't take up a row.
		}
		numSeen++;
		scroll = d;
	}
	return scroll;
}

void Devices::selectEncoderAction(int32_t offset) {
	offset = std::clamp<int32_t>(offset, -1, 1);

	// Remember where we started. This is always a connected device (beginSession and every completed
	// selectEncoderAction leave the selection on one), so if we run off the end while skipping disconnected
	// devices we can restore it rather than leaving the selection stranded on a disconnected/cached device.
	int32_t startValue = this->getValue();

	do {
		int32_t newValue = this->getValue() + offset;

		if (newValue >= static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size())) {
			if (display->haveOLED()) {
				this->setValue(startValue);
				soundEditor.currentMIDICable = getCable(startValue);
				return;
			}
			newValue = lowestDeviceNum;
		}
		else if (newValue < lowestDeviceNum) {
			if (display->haveOLED()) {
				this->setValue(startValue);
				soundEditor.currentMIDICable = getCable(startValue);
				return;
			}
			newValue = static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size()) - 1;
		}

		this->setValue(newValue);

		soundEditor.currentMIDICable = getCable(this->getValue());

	} while (!soundEditor.currentMIDICable->connectionFlags);
	// Don't show devices which aren't connected. Sometimes we won't even have a name to display for them.

	if (display->haveOLED()) {
		current_scroll_ = std::min(this->getValue(), current_scroll_);
		//
		if (offset >= 0) {
			int32_t d = this->getValue();
			int32_t numSeen = 1;
			while (d > lowestDeviceNum) {
				d--;
				if (d == current_scroll_) {
					break;
				}
				auto device = getCable(d);
				if (!(device && device->connectionFlags)) {
					continue;
				}
				numSeen++;
				if (numSeen >= kOLEDMenuNumOptionsVisible) {
					current_scroll_ = d;
					break;
				}
			}
		}
	}

	drawValue();
}

MIDICable* Devices::getCable(int32_t deviceIndex) {
	if (deviceIndex < lowestDeviceNum
	    || deviceIndex >= static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size())) {
		D_PRINTLN("impossible device request");
		return nullptr;
	}
	switch (deviceIndex) {
	case -3: {
		return &MIDIDeviceManager::dinMIDIPorts;
	}
	case -2: {
		return &MIDIDeviceManager::upstreamUSBMIDICable1;
	}
	case -1: {
		return &MIDIDeviceManager::upstreamUSBMIDICable2;
	}
	default: {
		return MIDIDeviceManager::hostedMIDIDevices[deviceIndex];
	}
	}
}

void Devices::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		char const* displayName = soundEditor.currentMIDICable->getDisplayName();
		display->setScrollingText(displayName);
	}
}

MenuItem* Devices::selectButtonPress() {
	return &midiDeviceMenu;
}

void Devices::drawPixelsForOled() {
	etl::vector<std::string_view, kOLEDMenuNumOptionsVisible> itemNames = {};

	int32_t selectedRow = -1;

	int32_t device_idx = current_scroll_;
	size_t row = 0;
	while (row < kOLEDMenuNumOptionsVisible
	       && device_idx < static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size())) {
		MIDICable* cable = getCable(device_idx);
		if (cable && cable->connectionFlags != 0u) {
			itemNames.push_back(cable->getDisplayName());
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
