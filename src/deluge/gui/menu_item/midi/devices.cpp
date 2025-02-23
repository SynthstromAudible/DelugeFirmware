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
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_root_complex.h"
#include <algorithm>
#include <etl/vector.h>
#include <string_view>

extern deluge::gui::menu_item::midi::Device midiDeviceMenu;

namespace deluge::gui::menu_item::midi {

static bool canSelectCable(MIDICable const* cable) {
	return cable != nullptr && cable->connectionFlags != 0;
}

/// Get the current maximum index for cables. This depends on what the current root USB device is.
static int32_t currentMaxCable() {
	if (MIDIDeviceManager::root_usb == nullptr) {
		return 0;
	}
	// n.b. we intentionally do not subtract 1 here since cable index 0 is the DIN ports  still
	return static_cast<int32_t>(MIDIDeviceManager::root_usb->getNumCables());
}

void Devices::beginSession(MenuItem* navigatedBackwardFrom) {
	bool found = false;
	if (navigatedBackwardFrom != nullptr) {
		// This will technically do the wrong thing when we're in peripheral mode (it'll set the max index to 2 instead
		// of 0, which would be accurate) but it should be harmless -- `Devices::getCable` should just return nullptr in
		// that case which we handle fine already anyway.
		auto max_index = currentMaxCable();
		for (int32_t idx = 0; idx < max_index; idx++) {
			if (getCable(idx) == soundEditor.currentMIDICable) {
				found = true;
				current_cable_ = idx;
				break;
			}
		}
	}

	if (!found) {
		// Start on "DIN". That's the only one that'll always be there.
		current_cable_ = 0;
	}

	soundEditor.currentMIDICable = getCable(current_cable_);

	// Update scroll position
	if (current_cable_ == 0) {
		scroll_pos_ = 0;
	}
	else if (current_cable_ >= kOLEDMenuNumOptionsVisible) {
		scroll_pos_ = kOLEDMenuNumOptionsVisible - 1;
	}
	else {
		scroll_pos_ = current_cable_;
	}

	// Redraw for 7seg
	if (!display->haveOLED()) {
		drawValue();
	}
}

void Devices::selectEncoderAction(int32_t offset) {
	offset = std::clamp<int32_t>(offset, -1, 1);

	// Find the next valid cable in the direction specified by `offset`
	int32_t max_index = currentMaxCable();
	int32_t new_index = current_cable_;
	MIDICable* new_cable = nullptr;
	do {
		new_index += offset;
		if (new_index > max_index) {
			if (display->haveOLED()) {
				return;
			}
			new_index = 0;
		}
		if (new_index < 0) {
			if (display->haveOLED()) {
				return;
			}
			new_index = max_index;
		}
		new_cable = getCable(new_index);
	} while (!canSelectCable(new_cable));

	// Write the cable to the sound editor and our state.
	current_cable_ = new_index;
	soundEditor.currentMIDICable = new_cable;

	// Figure out how to adjust the scroll position.
	if (offset > 0) {
		// If there are no valid cables after this one we're allowed to set the scroll position to the last line of the
		// OLED.
		bool seen_valid_cable = false;
		do {
			new_index++;
			new_cable = getCable(new_index);
			seen_valid_cable |= canSelectCable(new_cable);
		} while (new_index < max_index && !seen_valid_cable);

		if (seen_valid_cable) {
			scroll_pos_ = std::clamp<int32_t>(scroll_pos_ + offset, 0, kOLEDMenuNumOptionsVisible - 2);
		}
		else {
			scroll_pos_ = kOLEDMenuNumOptionsVisible - 1;
		}
	}
	else {
		// Since the DIN ports are always OK and always valid, we can just clamp the scroll position
		scroll_pos_ = std::max<int32_t>(scroll_pos_ + offset, 0);
	}

	drawValue();
}

MenuItem* Devices::selectButtonPress() {
	return &midiDeviceMenu;
}

MIDICable* Devices::getCable(int32_t deviceIndex) {
	if (deviceIndex == 0) {
		return &MIDIDeviceManager::root_din.cable;
	}

	if (MIDIDeviceManager::root_usb == nullptr) {
		return nullptr;
	}

	// USBRootComplex will return nullptr for out-of-range cables.
	return MIDIDeviceManager::root_usb->getCable(deviceIndex - 1);
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

void Devices::drawPixelsForOled() {
	// Collect all the item names
	etl::vector<std::string_view, kOLEDMenuNumOptionsVisible> item_names = {};

	// Fill in the items before the scroll position.
	int32_t cable_index = current_cable_ - 1;
	while (cable_index >= 0 && static_cast<int32_t>(item_names.size()) < scroll_pos_) {
		auto const* cable = getCable(cable_index);
		if (canSelectCable(cable)) {
			item_names.push_back(cable->getDisplayName());
		}
		cable_index--;
	}
	std::ranges::reverse(item_names.begin(), item_names.end());

	// Add the item at the scroll position
	item_names.push_back(getCable(current_cable_)->getDisplayName());

	// And fill in the items after the current cable.
	cable_index = current_cable_ + 1;
	auto max_index = currentMaxCable();
	while (cable_index <= max_index && item_names.size() < item_names.capacity()) {
		auto const* cable = getCable(cable_index);
		if (canSelectCable(cable)) {
			item_names.push_back(cable->getDisplayName());
		}
		cable_index++;
	}

	drawItemsForOled(item_names, scroll_pos_);
}

} // namespace deluge::gui::menu_item::midi
