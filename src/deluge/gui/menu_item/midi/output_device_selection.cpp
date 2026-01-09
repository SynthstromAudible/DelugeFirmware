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
#include "io/midi/midi_device_helper.h"
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
	// Get current device selection from MIDI instrument
	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
			this->setValue(midiInstrument->outputDevice);
		}
		else {
			this->setValue(0); // Default to ALL
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
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
	uint8_t currentDevice = static_cast<uint8_t>(this->getValue());

	// Get device name for storing
	auto deviceName = deluge::io::midi::getDeviceNameForIndex(currentDevice);

	// Update the actual device selection
	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
			midiInstrument->outputDevice = currentDevice;
			// Store device name for reliable matching when devices are reconnected
			if (!deviceName.empty()) {
				midiInstrument->outputDeviceName.set(deviceName.data());
			}
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
			auto* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
			midiDrum->outputDevice = currentDevice;
			// Store device name for reliable matching when devices are reconnected
			if (!deviceName.empty()) {
				midiDrum->outputDeviceName.set(deviceName.data());
			}
		}
	}
}

deluge::vector<std::string_view> OutputDeviceSelection::getOptions(OptType optType) {
	return deluge::io::midi::getAllMIDIDeviceNames();
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
