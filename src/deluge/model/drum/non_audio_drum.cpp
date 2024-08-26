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

#include "model/drum/non_audio_drum.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

NonAudioDrum::NonAudioDrum(DrumType newType) : Drum(newType) {
	state = false;
	channelEncoderCurrentOffset = 0;
}

bool NonAudioDrum::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	return true;
}

void NonAudioDrum::unassignAllVoices() {
	if (hasAnyVoices()) {
		noteOff(NULL);
	}
	arpeggiator.reset();
}

bool NonAudioDrum::anyNoteIsOn() {
	return state;
}

bool NonAudioDrum::hasAnyVoices() {
	return state;
}

/*
int8_t NonAudioDrum::getModKnobLevel(uint8_t whichModEncoder, ParamManagerBase* paramManager, uint32_t pos,
TimelineCounter* playPositionCounter) { if (whichModEncoder == 0) { channelEncoderCurrentOffset = 0;
    }
    return -64;
}
*/

int8_t NonAudioDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset,
                                      uint8_t whichModEncoder) {

	if ((getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) && (currentUIMode == UI_MODE_AUDITIONING)) {
		if (whichModEncoder == 0) {
			modChange(modelStack, offset, &channelEncoderCurrentOffset, &channel, getNumChannels());
		}
	}

	return -64;
}

extern int16_t zeroMPEValues[];

void NonAudioDrum::modChange(ModelStackWithThreeMainThings* modelStack, int32_t offset, int8_t* encoderOffset,
                             uint8_t* value, int32_t numValues) {

	*encoderOffset += offset;

	int32_t valueChange;
	if (*encoderOffset >= 4) {
		valueChange = 1;
	}
	else if (*encoderOffset <= -4) {
		valueChange = -1;
	}
	else {
		return;
	}

	bool wasOn = state;
	if (wasOn) {
		noteOff(NULL);
	}

	*encoderOffset = 0;

	int32_t newValue = (int32_t)*value + valueChange;
	if (newValue < 0) {
		newValue += numValues;
	}
	else if (newValue >= numValues) {
		newValue -= numValues;
	}

	*value = newValue;

	instrumentClipView.drawDrumName(this, true);

	if (wasOn) {
		noteOn(modelStack, lastVelocity, zeroMPEValues);
	}
}

void NonAudioDrum::writeArpeggiatorToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("arpeggiator");

	arpSettings.writeCommonParamsToFile(writer, nullptr);

	arpSettings.writeNonAudioParamsToFile(writer);

	writer.closeTag();
}

bool NonAudioDrum::readDrumTagFromFile(Deserializer& reader, char const* tagName) {

	if (!strcmp(tagName, "channel")) {
		channel = reader.readTagOrAttributeValueInt();
		reader.exitTag("channel");
	}
	else if (!strcmp(tagName, "arpeggiator")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			bool readAndExited = arpSettings.readCommonTagsFromFile(reader, tagName, nullptr);
			if (!readAndExited) {
				readAndExited = arpSettings.readNonAudioTagsFromFile(reader, tagName);
			}
			if (!readAndExited) {
				reader.exitTag(tagName);
			}
		}
		reader.match('}'); // End arpeggiator value object.
	}
	else if (Drum::readDrumTagFromFile(reader, tagName)) {}
	else {
		return false;
	}

	return true;
}
