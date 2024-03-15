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
#include "storage/storage_manager.h"
#include <string.h>

GateDrum::GateDrum() : NonAudioDrum(DrumType::GATE) {
	channel = 2;
}

void GateDrum::noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
                      int32_t fromMIDIChannel, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {
	cvEngine.sendNote(true, channel);
	state = true;
}

void GateDrum::noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity) {
	cvEngine.sendNote(false, channel);
	state = false;
}

void GateDrum::writeToFile(StorageManager &bdsm, bool savingSong, ParamManager* paramManager) {
	bdsm.writeOpeningTagBeginning("gateOutput");

	bdsm.writeAttribute("channel", channel, false);

	if (savingSong) {
		bdsm.writeOpeningTagEnd();
		Drum::writeMIDICommandsToFile(bdsm);
		bdsm.writeClosingTag("gateOutput");
	}
	else {
		bdsm.closeTag();
	}
}

Error GateDrum::readFromFile(StorageManager &bdsm, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = bdsm.readNextTagOrAttributeName())) {
		if (NonAudioDrum::readDrumTagFromFile(bdsm, tagName)) {}
		else {
			bdsm.exitTag(tagName);
		}
	}

	return Error::NONE;
}

void GateDrum::getName(char* buffer) {
	strcpy(buffer, "GAT");
	intToString(channel + 1, &buffer[3]);
}

int32_t GateDrum::getNumChannels() {
	return NUM_GATE_CHANNELS;
}
