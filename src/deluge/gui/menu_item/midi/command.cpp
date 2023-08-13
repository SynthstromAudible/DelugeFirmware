/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "command.h"
#include "definitions_cxx.hpp"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "util/misc.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace deluge::gui::menu_item::midi {

void Command::beginSession(MenuItem* navigatedBackwardFrom) {
	if (display.type != DisplayType::OLED) {
		drawValue();
	}
}

void Command::drawPixelsForOled() {
	LearnedMIDI* command = &midiEngine.globalMIDICommands[util::to_underlying(commandNumber)];
	int32_t yPixel = 20;
	if (!command->containsSomething()) {
		OLED::drawString(l10n::get(l10n::String::STRING_FOR_COMMAND_UNASSIGNED), 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextSpacingX,
		                 kTextSizeYUpdated);
	}
	else {
		char const* deviceString = l10n::get(l10n::String::STRING_FOR_ANY_MIDI_DEVICE);
		if (command->device) {
			deviceString = command->device->getDisplayName();
		}
		OLED::drawString(deviceString, 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextSpacingX,
		                 kTextSizeYUpdated);
		OLED::setupSideScroller(0, deviceString, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
		                        kTextSpacingX, kTextSpacingY, false);

		yPixel += kTextSpacingY;

		char const* channelText;
		if (command->channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE);
		}
		else if (command->channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE);
		}
		else {
			channelText = l10n::get(l10n::String::STRING_FOR_CHANNEL);
			char buffer[12];
			int32_t channelmod = (command->channelOrZone >= IS_A_CC) * IS_A_CC;
			intToString(command->channelOrZone + 1 - channelmod, buffer, 1);
			OLED::drawString(buffer, kTextSpacingX * 8, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
			                 kTextSpacingX, kTextSizeYUpdated);
		}
		OLED::drawString(channelText, 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextSpacingX,
		                 kTextSizeYUpdated);

		yPixel += kTextSpacingY;
		if (command->channelOrZone < IS_A_CC) {
			OLED::drawString("Note", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextSpacingX,
			                 kTextSizeYUpdated);
		}
		else {
			OLED::drawString("CC", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, kTextSpacingX,
			                 kTextSizeYUpdated);
		}

		char buffer[12];
		intToString(command->noteOrCC, buffer, 1);
		OLED::drawString(buffer, kTextSpacingX * 5, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 kTextSpacingX, kTextSizeYUpdated);
	}
}

void Command::drawValue() const {
	char const* output = nullptr;
	if (!midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].containsSomething()) {
		output = l10n::get(l10n::String::STRING_FOR_NONE);
	}
	else {
		output = l10n::get(l10n::String::STRING_FOR_SET);
	}
	display.setText(output);
}

void Command::selectEncoderAction(int32_t offset) {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].clear();
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void Command::unlearnAction() {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].clear();
	if (soundEditor.getCurrentMenuItem() == this) {
		if (display.type == DisplayType::OLED) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
	else {
		display.displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
	}
}

bool Command::learnNoteOn(MIDIDevice* device, int32_t channel, int32_t noteCode) {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].device = device;
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].channelOrZone = channel;
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].noteOrCC = noteCode;
	if (soundEditor.getCurrentMenuItem() == this) {
		if (display.type == DisplayType::OLED) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
	else {
		display.displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
	}
	return true;
}

void Command::learnCC(MIDIDevice* device, int32_t channel, int32_t ccNumber, int32_t value) {
	if (value != 0) {
		learnNoteOn(device, channel + IS_A_CC, ccNumber);
	}
}
} // namespace deluge::gui::menu_item::midi
