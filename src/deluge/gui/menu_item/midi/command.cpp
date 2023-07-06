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
#include "definitions.h"
#include "io/midi/midi_engine.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include "io/midi/midi_device.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace menu_item::midi {

void Command::beginSession(MenuItem* navigatedBackwardFrom) {
#if !HAVE_OLED
	drawValue();
#endif
}

#if HAVE_OLED
void Command::drawPixelsForOled() {
	LearnedMIDI* command = &midiEngine.globalMIDICommands[commandNumber];
	int yPixel = 20;
	if (!command->containsSomething()) {
		OLED::drawString("Command unassigned", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
	}
	else {
		char const* deviceString = "Any MIDI device";
		if (command->device) {
			deviceString = command->device->getDisplayName();
		}
		OLED::drawString(deviceString, 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
		                 TEXT_SIZE_Y_UPDATED);
		OLED::setupSideScroller(0, deviceString, TEXT_SPACING_X, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
		                        TEXT_SPACING_X, TEXT_SPACING_Y, false);

		yPixel += TEXT_SPACING_Y;

		char const* channelText;
		if (command->channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) {
			channelText = "MPE lower zone";
		}
		else if (command->channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
			channelText = "MPE upper zone";
		}
		else {
			channelText = "Channel";
			char buffer[12];
			int channelmod = (command->channelOrZone >= IS_A_CC) * IS_A_CC;
			intToString(command->channelOrZone + 1 - channelmod, buffer, 1);
			OLED::drawString(buffer, TEXT_SPACING_X * 8, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
			                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
		}
		OLED::drawString(channelText, 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
		                 TEXT_SIZE_Y_UPDATED);

		yPixel += TEXT_SPACING_Y;
		if (command->channelOrZone < IS_A_CC) {
			OLED::drawString("Note", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
			                 TEXT_SIZE_Y_UPDATED);
		}
		else {
			OLED::drawString("CC", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
			                 TEXT_SIZE_Y_UPDATED);
		}

		char buffer[12];
		intToString(command->noteOrCC, buffer, 1);
		OLED::drawString(buffer, TEXT_SPACING_X * 5, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
	}
}
#else
void Command::drawValue() {
	char const* output;
	if (!midiEngine.globalMIDICommands[commandNumber].containsSomething())
		output = "NONE";
	else
		output = "SET";
	numericDriver.setText(output);
}
#endif

void Command::selectEncoderAction(int offset) {
	midiEngine.globalMIDICommands[commandNumber].clear();
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}

void Command::unlearnAction() {
	midiEngine.globalMIDICommands[commandNumber].clear();
	if (soundEditor.getCurrentMenuItem() == this) {
#if HAVE_OLED
		renderUIsForOled();
#else
		drawValue();
#endif
	}
	else {
		numericDriver.displayPopup("UNLEARNED");
	}
}

bool Command::learnNoteOn(MIDIDevice* device, int channel, int noteCode) {
	midiEngine.globalMIDICommands[commandNumber].device = device;
	midiEngine.globalMIDICommands[commandNumber].channelOrZone = channel;
	midiEngine.globalMIDICommands[commandNumber].noteOrCC = noteCode;
	if (soundEditor.getCurrentMenuItem() == this) {
#if HAVE_OLED
		renderUIsForOled();
#else
		drawValue();
#endif
	}
	else {
		numericDriver.displayPopup("LEARNED");
	}
	return true;
}

void Command::learnCC(MIDIDevice* device, int channel, int ccNumber, int value) {
	if (value)
		learnNoteOn(device, channel + IS_A_CC, ccNumber);
}
} // namespace menu_item::midi
