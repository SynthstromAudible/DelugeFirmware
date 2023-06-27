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

#include "MenuItemMidiCommand.h"
#include "definitions.h"
#include "midiengine.h"
#include "soundeditor.h"
#include "numericdriver.h"
#include "oled.h"
#include "MIDIDevice.h"

extern "C" {
#include "cfunctions.h"
}

void MenuItemMidiCommand::beginSession(MenuItem* navigatedBackwardFrom) {
#if !HAVE_OLED
	drawValue();
#endif
}

#if HAVE_OLED
void MenuItemMidiCommand::drawPixelsForOled() {
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
		if (command->channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) channelText = "MPE lower zone";
		else if (command->channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) channelText = "MPE upper zone";
		else {
			channelText = "Channel";
			char buffer[12];
			intToString(command->channelOrZone + 1, buffer, 1);
			OLED::drawString(buffer, TEXT_SPACING_X * 8, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
			                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
		}
		OLED::drawString(channelText, 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
		                 TEXT_SIZE_Y_UPDATED);

		yPixel += TEXT_SPACING_Y;

		OLED::drawString("Note", 0, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X,
		                 TEXT_SIZE_Y_UPDATED);
		char buffer[12];
		intToString(command->noteOrCC, buffer, 1);
		OLED::drawString(buffer, TEXT_SPACING_X * 5, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SIZE_Y_UPDATED);
	}
}
#else
void MenuItemMidiCommand::drawValue() {
	char const* output;
	if (!midiEngine.globalMIDICommands[commandNumber].containsSomething()) output = "NONE";
	else output = "SET";
	numericDriver.setText(output);
}
#endif

void MenuItemMidiCommand::selectEncoderAction(int offset) {
	midiEngine.globalMIDICommands[commandNumber].clear();
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}

void MenuItemMidiCommand::unlearnAction() {
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

bool MenuItemMidiCommand::learnNoteOn(MIDIDevice* device, int channel, int noteCode) {
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

void MenuItemMidiCommand::learnCC(MIDIDevice* device, int channel, int ccNumber, int value) {
	if (MIDI_CC_FOR_COMMANDS_ENABLED && value) learnNoteOn(device, channel + 16, ccNumber);
}
