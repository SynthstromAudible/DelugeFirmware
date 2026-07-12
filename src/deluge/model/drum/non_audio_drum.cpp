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
#include "deluge-project/doc_drum.h"
#include "foundation/panic.h" // FREEZE_WITH_ERROR (M3 owner-3 unbound-read guard)
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

// M3 owner-3: read the channel from the authoritative DOM record (the drum is bound before any read — adopt precedes
// tag reads at load; runtime drums bind on addDrum). The ALPHA guard makes any stray unbound read fail loudly.
uint8_t NonAudioDrum::channel() const {
	if (ALPHA_OR_BETA_VERSION && docDrum_ == nullptr) {
		FREEZE_WITH_ERROR("Dr3C");
	}
	return docDrum_->channel;
}

// M3 owner-3: the write funnel for the channel — writes the authoritative `DocDrum::channel` (the drum is bound before
// any write). modChange writes `docDrum_->channel` in place through a raw `uint8_t*`.
void NonAudioDrum::setChannel(uint8_t newChannel) {
	docDrum_->channel = newChannel;
}

void NonAudioDrum::killAllVoices() {
	if (hasActiveVoices()) {
		noteOff(nullptr);
	}
	arpeggiator.reset();
}

int8_t NonAudioDrum::modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset,
                                      uint8_t whichModEncoder) {

	if ((getCurrentUI()->getUIContextType() == UIType::INSTRUMENT_CLIP) && (currentUIMode == UI_MODE_AUDITIONING)) {
		if (whichModEncoder == 0) {
			modChange(modelStack, offset, &channelEncoderCurrentOffset, &docDrum_->channel, getNumChannels());
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

	bool wasOn = state_;
	if (wasOn) {
		noteOff(nullptr);
	}

	*encoderOffset = 0;

	int32_t newValue = (int32_t)*value + valueChange;
	if (newValue < 0) {
		newValue += numValues;
	}
	else if (newValue >= numValues) {
		newValue -= numValues;
	}

	// M3 owner-3: `value` points straight at the authoritative DOM scalar (`&docDrum_->channel` from
	// NonAudioDrum::modEncoderAction, or `&docDrum_->note` from MIDIDrum), so writing it IS the model write.
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
		setChannel(reader.readTagOrAttributeValueInt());
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
