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

#include <LearnedMIDI.h>
#include "definitions.h"
#include "storagemanager.h"
#include <string.h>
#include "MIDIDevice.h"

extern "C" {
#include "cfunctions.h"
}

LearnedMIDI::LearnedMIDI() {
	clear();
}

void LearnedMIDI::clear() {
	device = NULL;
	channelOrZone = MIDI_CHANNEL_NONE;
	noteOrCC = 255;
}

char const* getTagNameFromMIDIMessageType(int midiMessageType) {
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

// If you're calling this direcly instead of calling writeToFile(), you'll need to check and possibly write a new tag for device - that can't be just an attribute.
// You should be sure that containsSomething() == true before calling this.
void LearnedMIDI::writeAttributesToFile(int midiMessageType) {

	if (isForMPEZone()) {
		char const* zoneText = (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE) ? "lower" : "upper";
		storageManager.writeAttribute("mpeZone", zoneText, false);
	}
	else {
		storageManager.writeAttribute("channel", channelOrZone, false);
	}

	if (midiMessageType != MIDI_MESSAGE_NONE) {
		char const* messageTypeName = getTagNameFromMIDIMessageType(midiMessageType);

		storageManager.writeAttribute(messageTypeName, noteOrCC, false);
	}
}

void LearnedMIDI::writeToFile(char const* commandName, int midiMessageType) {
	if (!containsSomething()) return;

	storageManager.writeOpeningTagBeginning(commandName);
	writeAttributesToFile(midiMessageType);

	if (device) {
		storageManager.writeOpeningTagEnd();
		device->writeReferenceToFile();
		storageManager.writeClosingTag(commandName);
	}
	else {
		storageManager.closeTag();
	}
}

void LearnedMIDI::readFromFile(int midiMessageType) {

	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "channel")) {
			channelOrZone = storageManager.readTagOrAttributeValueInt();
			channelOrZone = getMin((int)channelOrZone, 15);
		}
		else if (!strcmp(tagName, "mpeZone")) {
			readMPEZone();
		}
		else if (!strcmp(tagName, "device")) {
			device = MIDIDeviceManager::readDeviceReferenceFromFile();
		}
		else if (midiMessageType != MIDI_MESSAGE_NONE
		         && !strcmp(tagName, getTagNameFromMIDIMessageType(midiMessageType))) {
			noteOrCC = storageManager.readTagOrAttributeValueInt();
			noteOrCC = getMin((int)noteOrCC, 127);
		}
		storageManager.exitTag();
	}
}

void LearnedMIDI::readMPEZone() {
	char const* text = storageManager.readTagOrAttributeValue();
	if (!strcmp(text, "lower")) channelOrZone = MIDI_CHANNEL_MPE_LOWER_ZONE;
	else if (!strcmp(text, "upper")) channelOrZone = MIDI_CHANNEL_MPE_UPPER_ZONE;
}

bool LearnedMIDI::equalsChannelAllowMPE(MIDIDevice* newDevice, int newChannel) {
	if (channelOrZone == MIDI_CHANNEL_NONE)
		return false; // 99% of the time, we'll get out here, because input isn't activated/learned.
	if (!equalsDevice(newDevice)) return false;
	if (channelOrZone < 16) return (channelOrZone == newChannel);
	if (!device)
		return false; // Could we actually be set to MPE but have no device? Maybe if loaded from weird song file?
	if (channelOrZone == MIDI_CHANNEL_MPE_LOWER_ZONE)
		return (newChannel <= device->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel);
	if (channelOrZone == MIDI_CHANNEL_MPE_UPPER_ZONE)
		return (newChannel >= device->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel);
	return false; // Theoretically I don't think we'd ever get here...
}

bool LearnedMIDI::equalsChannelAllowMPEMasterChannels(MIDIDevice* newDevice, int newChannel) {
	if (channelOrZone == MIDI_CHANNEL_NONE)
		return false; // 99% of the time, we'll get out here, because input isn't activated/learned.
	if (!equalsDevice(newDevice)) return false;
	if (channelOrZone < 16) return (channelOrZone == newChannel);
	return (newChannel == getMasterChannel());
}
