/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/views/instrument_clip_view.h"
#include "model/drum/midi_drum.h"
#include "storage/storage_manager.h"
#include <string.h>
#include "io/midi/midi_engine.h"
#include "util/functions.h"

extern "C" {
#include "util/cfunctions.h"
}

MIDIDrum::MIDIDrum() : NonAudioDrum(DrumType::MIDI) {
	channel = 0;
	note = 0;
}

void MIDIDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
                      int fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	lastVelocity = velocity;
	midiEngine.sendNote(true, note, velocity, channel, MIDI_OUTPUT_FILTER_NO_MPE);
	state = true;
}

void MIDIDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int velocity) {
	midiEngine.sendNote(false, note, velocity, channel, MIDI_OUTPUT_FILTER_NO_MPE);
	state = false;
}

void MIDIDrum::unassignAllVoices() {
	if (hasAnyVoices()) {
		noteOff(NULL);
	}
}

void MIDIDrum::writeToFile(bool savingSong, ParamManager* paramManager) {
	storageManager.writeOpeningTagBeginning("midiOutput");

	storageManager.writeAttribute("channel", channel, false);
	storageManager.writeAttribute("note", note, false);

	if (savingSong) {
		storageManager.writeOpeningTagEnd();
		Drum::writeMIDICommandsToFile();
		storageManager.writeClosingTag("midiOutput");
	}
	else {
		storageManager.closeTag();
	}
}

int MIDIDrum::readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "note")) {
			note = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("note");
		}
		else if (NonAudioDrum::readDrumTagFromFile(tagName)) {}
		else {
			storageManager.exitTag(tagName);
		}
	}

	return NO_ERROR;
}

void MIDIDrum::getName(char* buffer) {

	int channelToDisplay = channel + 1;

	if (channelToDisplay < 10 && note < 100) {
		strcpy(buffer, " ");
	}
	else {
		buffer[0] = 0;
	}

	// If can't fit everything on display, show channel as hexadecimal
	if (channelToDisplay >= 10 && note >= 100) {
		buffer[0] = 'A' + channelToDisplay - 10;
		buffer[1] = 0;
	}
	else {
		intToString(channelToDisplay, &buffer[strlen(buffer)]);
	}

	strcat(buffer, ".");

	if (note < 10 && channelToDisplay < 100) {
		strcat(buffer, " ");
	}

	intToString(note, &buffer[strlen(buffer)]);
}

int8_t MIDIDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder) {

	NonAudioDrum::modEncoderAction(modelStack, offset, whichModEncoder);

	if (getCurrentUI() == &instrumentClipView && currentUIMode == UI_MODE_AUDITIONING) {
		if (whichModEncoder == 1) {
			modChange(modelStack, offset, &noteEncoderCurrentOffset, &note, 128);
		}
	}

	return -64;
}

void MIDIDrum::expressionEvent(int newValue, int whichExpressionDimension) {

	// Aftertouch only
	if (whichExpressionDimension == 2) {
		int value7 = newValue >> 24;
		midiEngine.sendPolyphonicAftertouch(channel, value7, note, MIDI_OUTPUT_FILTER_NO_MPE);
	}
}

void MIDIDrum::polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension,
                                                        int channelOrNoteNumber, MIDICharacteristic whichCharacteristic) {
	// Because this is a Drum, we disregard the noteCode (which is what channelOrNoteNumber always is in our case - but yeah, that's all irrelevant.
	expressionEvent(newValue, whichExpressionDimension);
}
