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

#include <InstrumentClipView.h>
#include "NonAudioDrum.h"
#include "UI.h"
#include "storagemanager.h"
#include "functions.h"

NonAudioDrum::NonAudioDrum(int newType) : Drum(newType) {
	state = false;
	channelEncoderCurrentOffset = 0;
}

bool NonAudioDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return true;
}

void NonAudioDrum::unassignAllVoices() {
	if (hasAnyVoices()) noteOff(NULL);
}

bool NonAudioDrum::anyNoteIsOn() {
	return state;
}

bool NonAudioDrum::hasAnyVoices() {
	return state;
}

/*
int8_t NonAudioDrum::getModKnobLevel(uint8_t whichModEncoder, ParamManagerBase* paramManager, uint32_t pos, TimelineCounter* playPositionCounter) {
	if (whichModEncoder == 0) {
		channelEncoderCurrentOffset = 0;
	}
	return -64;
}
*/

int8_t NonAudioDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset,
                                      uint8_t whichModEncoder) {

	if (getCurrentUI() == &instrumentClipView && currentUIMode == UI_MODE_AUDITIONING) {
		if (whichModEncoder == 0) {
			modChange(modelStack, offset, &channelEncoderCurrentOffset, &channel, getNumChannels());
		}
	}

	return -64;
}

extern int16_t zeroMPEValues[];

void NonAudioDrum::modChange(ModelStackWithThreeMainThings* modelStack, int offset, int8_t* encoderOffset,
                             uint8_t* value, int numValues) {

	*encoderOffset += offset;

	int valueChange;
	if (*encoderOffset >= 4) valueChange = 1;
	else if (*encoderOffset <= -4) valueChange = -1;
	else return;

	bool wasOn = state;
	if (wasOn) noteOff(NULL);

	*encoderOffset = 0;

	int newValue = (int)*value + valueChange;
	if (newValue < 0) newValue += numValues;
	else if (newValue >= numValues) newValue -= numValues;

	*value = newValue;

	instrumentClipView.drawDrumName(this, true);

	if (wasOn) {
		noteOn(modelStack, lastVelocity, NULL, zeroMPEValues);
	}
}

bool NonAudioDrum::readDrumTagFromFile(char const* tagName) {

	if (!strcmp(tagName, "channel")) {
		channel = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("channel");
	}
	else if (Drum::readDrumTagFromFile(tagName)) {}
	else return false;

	return true;
}
