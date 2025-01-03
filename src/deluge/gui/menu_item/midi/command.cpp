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
#include "hid/display/oled.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "util/cfunctions.h"
#include "util/misc.h"
#include "util/string.h"
#include <string_view>

namespace deluge::gui::menu_item::midi {

void Command::beginSession(MenuItem* navigatedBackwardFrom) {
	if (display->have7SEG()) {
		drawValue();
	}
}

void Command::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
	LearnedMIDI* command = &midiEngine.globalMIDICommands[util::to_underlying(commandNumber)];
	int32_t yPixel = 20;
	if (!command->containsSomething()) {
		image.drawString(l10n::get(l10n::String::STRING_FOR_COMMAND_UNASSIGNED), 0, yPixel, kTextSpacingX,
		                 kTextSizeYUpdated);
	}
	else {
		std::string_view deviceString = l10n::get(l10n::String::STRING_FOR_ANY_MIDI_DEVICE);
		if (command->cable) {
			deviceString = command->cable->getDisplayName();
		}
		image.drawString(deviceString, 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
		deluge::hid::display::OLED::setupSideScroller(0, deviceString, kTextSpacingX, OLED_MAIN_WIDTH_PIXELS, yPixel,
		                                              yPixel + 8, kTextSpacingX, kTextSpacingY, false);

		yPixel += kTextSpacingY;

		std::string_view channelText;
		if (command->channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_LOWER_ZONE);
		}
		else if (command->channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = l10n::get(l10n::String::STRING_FOR_MPE_UPPER_ZONE);
		}
		else {
			channelText = l10n::get(l10n::String::STRING_FOR_CHANNEL);
			int32_t channelmod = 0;
			if (command->channelOrZone >= IS_A_PC) {
				channelmod = IS_A_PC; // the great CC channel hack extended
			}
			else if (command->channelOrZone >= IS_A_CC) {
				channelmod = IS_A_CC;
			}

			image.drawString(string::fromInt(command->channelOrZone + 1 - channelmod), kTextSpacingX * 8, yPixel,
			                 kTextSpacingX, kTextSizeYUpdated);
		}
		image.drawString(channelText, 0, yPixel, kTextSpacingX, kTextSizeYUpdated);

		yPixel += kTextSpacingY;
		if (command->channelOrZone < IS_A_CC) {
			image.drawString("Note", 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
		}
		else if (command->channelOrZone < IS_A_PC) {
			image.drawString("CC", 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
		}
		else {
			image.drawString("PC", 0, yPixel, kTextSpacingX, kTextSizeYUpdated);
		}

		image.drawString(string::fromInt(command->noteOrCC), kTextSpacingX * 5, yPixel, kTextSpacingX,
		                 kTextSizeYUpdated);
	}
}

void Command::drawValue() const {
	std::string_view output;
	if (!midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].containsSomething()) {
		output = l10n::get(l10n::String::STRING_FOR_NONE);
	}
	else {
		output = l10n::get(l10n::String::STRING_FOR_SET);
	}
	display->setText(output);
}

void Command::selectEncoderAction(int32_t offset) {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].clear();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void Command::unlearnAction() {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].clear();
	if (soundEditor.getCurrentMenuItem() == this) {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNLEARNED));
	}
}

void Command::learnProgramChange(MIDICable& cable, int32_t channel, int32_t programNumber) {
	if (commandNumber == GlobalMIDICommand::FILL) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CANT_LEARN_PC));
	}
	else {
		learnNoteOn(cable, channel + IS_A_PC, programNumber);
	}
}

bool Command::learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) {
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].cable = &cable;
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].channelOrZone = channel;
	midiEngine.globalMIDICommands[util::to_underlying(commandNumber)].noteOrCC = noteCode;
	if (soundEditor.getCurrentMenuItem() == this) {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
	else {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_LEARNED));
	}
	return true;
}

void Command::learnCC(MIDICable& cable, int32_t channel, int32_t ccNumber, int32_t value) {
	if (value != 0) {
		learnNoteOn(cable, channel + IS_A_CC, ccNumber);
	}
}
} // namespace deluge::gui::menu_item::midi
