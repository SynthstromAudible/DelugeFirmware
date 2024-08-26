/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/menu_item/toggle.h"
#include "gui/ui/load/load_midi_device_definition_ui.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi::device_definition {

class Linked : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override {
		MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
		this->setValue(!midiInstrument->deviceDefinitionFileName.isEmpty());
	}
	void writeCurrentValue() override {
		t = this->getValue();

		// if you want to link a definition file, open the load definition file UI
		if (t) {
			openUI(&loadMidiDeviceDefinitionUI);
		}
		// if you want to unlink a definition file, just clear the definition file name
		else {
			MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();
			midiInstrument->deviceDefinitionFileName.clear();
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
		Output* output = getCurrentOutput();
		return (output && output->type == OutputType::MIDI_OUT);
	}

	void renderSubmenuItemTypeForOled(int32_t yPixel) final {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

		int32_t startX = getSubmenuItemTypeRenderIconStart();

		if (getToggleValue()) {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::checkedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);

			MIDIInstrument* midiInstrument = (MIDIInstrument*)getCurrentOutput();

			char const* fullPath = midiInstrument->deviceDefinitionFileName.get();

			// locate last occurence of "/" in string
			char* fileName = strrchr((char*)fullPath, '/');

			image.drawString(++fileName, kTextSpacingX, yPixel + kTextSpacingY, kTextSpacingX, kTextSpacingY);
		}
		else {
			image.drawGraphicMultiLine(deluge::hid::display::OLED::uncheckedBoxIcon, startX, yPixel,
			                           kSubmenuIconSpacingX);
		}
	}

	bool t;
};

} // namespace deluge::gui::menu_item::midi::device_definition
