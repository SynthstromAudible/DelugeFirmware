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

#pragma once

#include "io/midi/midi_device_manager.h"
#include <cstdint>

#define MIDI_MESSAGE_NONE 0
#define MIDI_MESSAGE_NOTE 1
#define MIDI_MESSAGE_CC 2

class MIDIDevice;
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MEMBER, MPE_MASTER };
class LearnedMIDI {
public:
	LearnedMIDI();
	void clear();

	inline bool equalsDevice(MIDIDevice* newDevice) {
		return (!MIDIDeviceManager::differentiatingInputsByDevice || !device || newDevice == device);
	}

	inline bool equalsChannelOrZone(MIDIDevice* newDevice, int32_t newChannelOrZone) {
		return (newChannelOrZone == channelOrZone && equalsDevice(newDevice));
	}

	inline bool equalsNoteOrCC(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelOrZone(newDevice, newChannel));
	}

	bool equalsChannelAllowMPE(MIDIDevice* newDevice, int32_t newChannel);
	bool equalsChannelAllowMPEMasterChannels(MIDIDevice* newDevice, int32_t newChannel);

	// Check that note or CC and channel match, does not check if channel in MPE zone
	inline bool equalsNoteOrCCAllowMPE(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPE(newDevice, newChannel));
	}

	inline bool equalsNoteOrCCAllowMPEMasterChannels(MIDIDevice* newDevice, int32_t newChannel, int32_t newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPEMasterChannels(newDevice, newChannel));
	}
	MIDIMatchType checkMatch(MIDIDevice* fromDevice, int32_t channel);
	inline bool containsSomething() { return (channelOrZone != MIDI_CHANNEL_NONE); }

	// You must have determined that containsSomething() == true before calling this.
	inline bool isForMPEZone() { return (channelOrZone >= 16); }

	// You must have determined that isForMPEZone() == true before calling this.
	inline int32_t getMasterChannel() { return (channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE) * 15; }

	void writeAttributesToFile(int32_t midiMessageType);
	void writeToFile(char const* commandName,
	                 int32_t midiMessageType); // Writes the actual tag in addition to the attributes
	void readFromFile(int32_t midiMessageType);

	inline void writeNoteToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_NOTE); }
	inline void writeCCToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_CC); }
	inline void writeChannelToFile(char const* commandName) { writeToFile(commandName, MIDI_MESSAGE_NONE); }

	inline void readNoteFromFile() { readFromFile(MIDI_MESSAGE_NOTE); }
	inline void readCCFromFile() { readFromFile(MIDI_MESSAGE_CC); }
	inline void readChannelFromFile() { readFromFile(MIDI_MESSAGE_NONE); }

	void readMPEZone();

	MIDIDevice* device;
	// In addition to being set to channel 0 to 15, can also be MIDI_CHANNEL_MPE_LOWER_ZONE or
	// MIDI_CHANNEL_MPE_UPPER_ZONE. Channels 18-36 signify CCs on channels 1-16+MPE respectively. Check with the
	// constant IS_A_CC
	uint8_t channelOrZone;
	uint8_t noteOrCC;
};
