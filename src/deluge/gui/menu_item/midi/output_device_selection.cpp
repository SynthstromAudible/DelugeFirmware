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

#include "gui/menu_item/midi/output_device_selection.h"
#include "RZA1/cpu_specific.h" // Added for OLED constants
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "io/midi/midi_device_manager.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h" // Added for getCurrentInstrument()

namespace deluge::gui::menu_item::midi {

void OutputDeviceSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	Selection::beginSession(navigatedBackwardFrom);
	readCurrentValue();
}

void OutputDeviceSelection::readCurrentValue() {
	// Determine current device selection based on context
	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument && instrument->type == OutputType::MIDI_OUT) {
			auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
			if (midiInstrument->outputDeviceMask == 0) {
				this->setValue(0); // ALL devices
			}
			else {
				// Find the first set bit
				for (int32_t i = 0; i < 32; i++) {
					if (midiInstrument->outputDeviceMask & (1 << i)) {
						this->setValue(i + 1);
						break;
					}
				}
			}
		}
		else {
			this->setValue(0); // Default to ALL
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit && kit->selectedDrum && kit->selectedDrum->type == DrumType::MIDI) {
			auto* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
			this->setValue(midiDrum->outputDevice);
		}
		else {
			this->setValue(0); // Default to ALL
		}
	}
	else {
		this->setValue(0); // Default to ALL
	}
}

void OutputDeviceSelection::writeCurrentValue() {
	int32_t currentDevice = this->getValue();

	// Update the actual device selection
	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument && instrument->type == OutputType::MIDI_OUT) {
			auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
			if (currentDevice == 0) {
				midiInstrument->outputDeviceMask = 0; // ALL devices
			}
			else {
				midiInstrument->outputDeviceMask = (1 << (currentDevice - 1));
			}
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit && kit->selectedDrum && kit->selectedDrum->type == DrumType::MIDI) {
			auto* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
			midiDrum->outputDevice = currentDevice;
		}
	}
}

deluge::vector<std::string_view> OutputDeviceSelection::getOptions(OptType optType) {
	(void)optType; // Not used for this implementation

	deluge::vector<std::string_view> options;

	// Always include ALL (0)
	options.push_back("ALL");

	// Always include DIN (1) - get the actual DIN cable name
	MIDICable* dinCable = &MIDIDeviceManager::root_din.cable;
	options.push_back(dinCable->getDisplayName());

	// Add USB devices using the same logic as the main MIDI devices menu
	if (MIDIDeviceManager::root_usb != nullptr) {
		int32_t numCables = MIDIDeviceManager::root_usb->getNumCables();
		for (int32_t i = 0; i < numCables; i++) {
			MIDICable* usbCable = MIDIDeviceManager::root_usb->getCable(i);
			if (usbCable) {
				options.push_back(usbCable->getDisplayName());
			}
		}
	}

	return options;
}

void OutputDeviceSelection::getDeviceName(int32_t deviceIndex, StringBuf& buffer) const {
	// Implementation for getting device names (if needed later)
	// For now, just use simple USB names
}

void OutputDeviceSelection::drawValue() {
	if (display->haveOLED()) {
		deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;

		int32_t current = getValue();
		const auto options = this->getOptions(OptType::FULL);
		int32_t numOptions = options.size();

		// Debug: always show at least 3 options regardless of current value
		int32_t startIndex = 0;
		int32_t endIndex = etl::min<int32_t>(numOptions, 3);

		int32_t yPixel = OLED_MAIN_TOPMOST_PIXEL + 15;

		for (int32_t i = startIndex; i < endIndex; i++) {
			const auto& option = options[i];

			// Draw checkbox
			int32_t checkboxX = 5;
			if (i == current) {
				// Selected item - draw checked box
				canvas.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, checkboxX, yPixel,
				                            kSubmenuIconSpacingX);
			}
			else {
				// Unselected item - draw unchecked box
				canvas.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, checkboxX, yPixel,
				                            kSubmenuIconSpacingX);
			}

			// Draw option text
			int32_t textX = checkboxX + kSubmenuIconSpacingX + 5;
			canvas.drawString(option.data(), textX, yPixel, kTextSpacingX, kTextSpacingY);

			// Highlight selected item
			if (i == current) {
				canvas.invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8);
			}

			yPixel += kTextSpacingY;
		}
	}
	else {
		// 7SEG display - show current option
		const auto options = this->getOptions(OptType::FULL);
		display->setScrollingText(options[this->getValue()].data());
	}
}

} // namespace deluge::gui::menu_item::midi
