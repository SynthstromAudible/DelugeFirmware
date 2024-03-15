/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "io/midi/learned_midi.h"
#include "definitions_cxx.hpp"
#include "io/midi/midi_device.h"
#include "storage/storage_manager.h"
#include <string.h>

LearnedMIDI::LearnedMIDI() {
	clear();
}
MIDIMatchType LearnedMIDI::checkMatch(MIDIDevice* fromDevice, int32_t midiChannel) {
	uint8_t corz = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(midiChannel);

	if (equalsDevice(fromDevice) && channelOrZone == corz) {
		if (channelOrZone == midiChannel) {
			return MIDIMatchType::CHANNEL;
		}
		bool master = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isMasterChannel(midiChannel);
		if (master) {
			return MIDIMatchType::MPE_MASTER;
		}
		else {
			return MIDIMatchType::MPE_MEMBER;
		}
	}
	return MIDIMatchType::NO_MATCH;
}

void LearnedMIDI::clear() {
	device = NULL;
	channelOrZone = MIDI_CHANNEL_NONE;
	noteOrCC = 255;
}

char const* getTagNameFromMIDIMessageType(int32_t midiMessageType) {
	switch (midiMessageType) {
	case MIDI_MESSAGE_NOTE:
		return "note";

	case MIDI_MESSAGE_CC:
		return "ccNumber";

	default:
		__builtin_unreachable();
		return NULL;
	}
}

// If you're calling this direcly instead of calling writeToFile(), you'll need to check and possibly write a new tag
// for device - that can't be just an attribute. You should be sure that containsSomething() == true before calling
// this.
void LearnedMIDI::writeAttributesToFile(StorageManager& bdsm, int32_t midiMessageType) {

	if (isForMPEZone()) {
		char const* zoneText = (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "lower" : "upper";
		bdsm.writeAttribute("mpeZone", zoneText, false);
	}
	else {
		bdsm.writeAttribute("channel", channelOrZone, false);
	}

	if (midiMessageType != MIDI_MESSAGE_NONE) {
		char const* messageTypeName = getTagNameFromMIDIMessageType(midiMessageType);

		bdsm.writeAttribute(messageTypeName, noteOrCC, false);
	}
}

void LearnedMIDI::writeToFile(StorageManager& bdsm, char const* commandName, int32_t midiMessageType) {
	if (!containsSomething()) {
		return;
	}

	bdsm.writeOpeningTagBeginning(commandName);
	writeAttributesToFile(bdsm, midiMessageType);

	if (device) {
		bdsm.writeOpeningTagEnd();
		device->writeReferenceToFile(bdsm);
		bdsm.writeClosingTag(commandName);
	}
	else {
		bdsm.closeTag();
	}
}

void LearnedMIDI::readFromFile(StorageManager& bdsm, int32_t midiMessageType) {

	char const* tagName;
	while (*(tagName = bdsm.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "channel")) {
			channelOrZone = bdsm.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "mpeZone")) {
			readMPEZone(bdsm);
		}
		else if (!strcmp(tagName, "device")) {
			device = MIDIDeviceManager::readDeviceReferenceFromFile(bdsm);
		}
		else if (midiMessageType != MIDI_MESSAGE_NONE
		         && !strcmp(tagName, getTagNameFromMIDIMessageType(midiMessageType))) {
			noteOrCC = bdsm.readTagOrAttributeValueInt();
			noteOrCC = std::min<int32_t>(noteOrCC, 127);
		}
		bdsm.exitTag();
	}
}

void LearnedMIDI::readMPEZone(StorageManager& bdsm) {
	char const* text = bdsm.readTagOrAttributeValue();
	if (!strcmp(text, "lower")) {
		channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
	}
	else if (!strcmp(text, "upper")) {
		channelOrZone = MIDI_CHANNEL_MPE_UPPER_ZONE;
	}
}

bool LearnedMIDI::equalsChannelAllowMPE(MIDIDevice* newDevice, int32_t newChannel) {
	if (channelOrZone == MIDI_CHANNEL_NONE) {
		return false; // 99% of the time, we'll get out here, because input isn't activated/learned.
	}
	if (!equalsDevice(newDevice)) {
		return false;
	}
	if (!device) {
		return false; // Could we actually be set to MPE but have no device? Maybe if loaded from weird song file?
	}
	int32_t newCorZ = newDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(newChannel);

	return (channelOrZone == newCorZ);
}

bool LearnedMIDI::equalsChannelAllowMPEMasterChannels(MIDIDevice* newDevice, int32_t newChannel) {
	if (channelOrZone == MIDI_CHANNEL_NONE) {
		return false; // 99% of the time, we'll get out here, because input isn't activated/learned.
	}
	if (!equalsDevice(newDevice)) {
		return false;
	}
	if (channelOrZone < 16) {
		return (channelOrZone == newChannel);
	}
	if (channelOrZone > IS_A_CC) {
		return (channelOrZone == newChannel);
	}

	if (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) {
		if (newChannel <= newDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel) {
			return (newChannel == getMasterChannel());
		}
	}
	if (channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
		if (newChannel >= newDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel) {
			return (newChannel == getMasterChannel());
		}
	}
	return false; // should never happen
}
