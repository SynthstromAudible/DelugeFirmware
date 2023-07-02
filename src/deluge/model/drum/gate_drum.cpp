/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "model/drum/gate_drum.h"
#include "processing/engines/cv_engine.h"
#include <string.h>
#include "util/functions.h"
#include "storage/storage_manager.h"

extern "C" {
#include "util/cfunctions.h"
}

GateDrum::GateDrum() : NonAudioDrum(DRUM_TYPE_GATE) {
	channel = 2;
}

void GateDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
                      int fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	cvEngine.sendNote(true, channel);
	state = true;
}

void GateDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int velocity) {
	cvEngine.sendNote(false, channel);
	state = false;
}

void GateDrum::writeToFile(bool savingSong, ParamManager* paramManager) {
	storageManager.writeOpeningTagBeginning("gateOutput");

	storageManager.writeAttribute("channel", channel, false);

	if (savingSong) {
		storageManager.writeOpeningTagEnd();
		Drum::writeMIDICommandsToFile();
		storageManager.writeClosingTag("gateOutput");
	}
	else {
		storageManager.closeTag();
	}
}

int GateDrum::readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (NonAudioDrum::readDrumTagFromFile(tagName)) {}
		else storageManager.exitTag(tagName);
	}

	return NO_ERROR;
}

void GateDrum::getName(char* buffer) {
	strcpy(buffer, "GAT");
	intToString(channel + 1, &buffer[3]);
}

int GateDrum::getNumChannels() {
	return NUM_GATE_CHANNELS;
}
