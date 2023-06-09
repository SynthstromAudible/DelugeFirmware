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

#ifndef LEARNEDMIDI_H_
#define LEARNEDMIDI_H_

#include "r_typedefs.h"
#include "MIDIDeviceManager.h"

#define MIDI_MESSAGE_NONE 0
#define MIDI_MESSAGE_NOTE 1
#define MIDI_MESSAGE_CC 2

class MIDIDevice;

class LearnedMIDI {
public:
	LearnedMIDI();
	void clear();

	inline bool equalsDevice(MIDIDevice* newDevice) {
		return (!MIDIDeviceManager::differentiatingInputsByDevice || !device || newDevice == device);
	}

	inline bool equalsChannelOrZone(MIDIDevice* newDevice, int newChannelOrZone) {
		return (newChannelOrZone == channelOrZone && equalsDevice(newDevice));
	}

	inline bool equalsNoteOrCC(MIDIDevice* newDevice, int newChannel, int newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelOrZone(newDevice, newChannel));
	}

	bool equalsChannelAllowMPE(MIDIDevice* newDevice, int newChannel);
	bool equalsChannelAllowMPEMasterChannels(MIDIDevice* newDevice, int newChannel);

	//Check that note or CC and channel match, does not check if channel in MPE zone
	inline bool equalsNoteOrCCAllowMPE(MIDIDevice* newDevice, int newChannel, int newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPE(newDevice, newChannel));
	}

	inline bool equalsNoteOrCCAllowMPEMasterChannels(MIDIDevice* newDevice, int newChannel, int newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPEMasterChannels(newDevice, newChannel));
	}

	inline bool containsSomething() { return (channelOrZone != MIDI_CHANNEL_NONE); }

	// You must have determined that containsSomething() == true before calling this.
	inline bool isForMPEZone() { return (channelOrZone >= 16); }

	// You must have determined that isForMPEZone() == true before calling this.
	inline int getMasterChannel() { return (channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE) * 15; }

	void writeAttributesToFile(int midiMessageType);
	void writeToFile(char const* commandName,
	                 int midiMessageType); // Writes the actual tag in addition to the attributes
	void readFromFile(int midiMessageType);

	inline void writeNoteToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_NOTE); }
	inline void writeCCToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_CC); }
	inline void writeChannelToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_NONE); }

	inline void readNoteFromFile() { readFromFile(MIDI_MESSAGE_NOTE); }
	inline void readCCFromFile() { readFromFile(MIDI_MESSAGE_CC); }
	inline void readChannelFromFile() { readFromFile(MIDI_MESSAGE_NONE); }

	void readMPEZone();

	MIDIDevice* device;
	uint8_t
	    channelOrZone; // In addition to being set to channel 0 to 15, can also be MIDI_CHANNEL_MPE_LOWER_ZONE or MIDI_CHANNEL_MPE_UPPER_ZONE
	uint8_t noteOrCC;
};

#endif /* LEARNEDMIDI_H_ */
