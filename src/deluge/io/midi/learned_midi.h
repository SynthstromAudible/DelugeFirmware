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
#include "storage/storage_manager.h"
#include <cstdint>

#define MIDI_MESSAGE_NONE 0
#define MIDI_MESSAGE_NOTE 1
#define MIDI_MESSAGE_CC 2

class MIDICable;
enum class MIDIMatchType { NO_MATCH, CHANNEL, MPE_MEMBER, MPE_MASTER };
class LearnedMIDI {
public:
	LearnedMIDI();
	void clear();

	constexpr bool equalsCable(MIDICable* newCable) const {
		return (!MIDIDeviceManager::differentiatingInputsByDevice || !cable || newCable == cable);
	}

	constexpr bool equalsChannelOrZone(MIDICable* newCable, int32_t newChannelOrZone) const {
		return (newChannelOrZone == channelOrZone && equalsCable(newCable));
	}

	constexpr bool equalsNoteOrCC(MIDICable* newCable, int32_t newChannel, int32_t newNoteOrCC) const {
		return (newNoteOrCC == noteOrCC && equalsChannelOrZone(newCable, newChannel));
	}

	bool equalsChannelAllowMPE(MIDICable* newCable, int32_t newChannel);
	bool equalsChannelAllowMPEMasterChannels(MIDICable* newCable, int32_t newChannel);

	// Check that note or CC and channel match, does not check if channel in MPE zone
	inline bool equalsNoteOrCCAllowMPE(MIDICable* newCable, int32_t newChannel, int32_t newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPE(newCable, newChannel));
	}

	inline bool equalsNoteOrCCAllowMPEMasterChannels(MIDICable* newCable, int32_t newChannel, int32_t newNoteOrCC) {
		return (newNoteOrCC == noteOrCC && equalsChannelAllowMPEMasterChannels(newCable, newChannel));
	}
	MIDIMatchType checkMatch(MIDICable* fromCable, int32_t channel);
	inline bool containsSomething() { return (channelOrZone != MIDI_CHANNEL_NONE); }

	// You must have determined that containsSomething() == true before calling this.
	inline bool isForMPEZone() { return (channelOrZone >= 16); }

	// You must have determined that isForMPEZone() == true before calling this.
	inline int32_t getMasterChannel() { return (channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE) * 15; }

	void writeAttributesToFile(Serializer& writer, int32_t midiMessageType);
	void writeToFile(Serializer& writer, char const* commandName,
	                 int32_t midiMessageType); // Writes the actual tag in addition to the attributes
	void readFromFile(Deserializer& reader, int32_t midiMessageType);

	inline void writeNoteToFile(Serializer& writer, char const* commandName) {
		writeToFile(writer, commandName, MIDI_MESSAGE_NOTE);
	}
	inline void writeCCToFile(Serializer& writer, char const* commandName) {
		writeToFile(writer, commandName, MIDI_MESSAGE_CC);
	}
	inline void writeChannelToFile(Serializer& writer, char const* commandName) {
		writeToFile(writer, commandName, MIDI_MESSAGE_NONE);
	}

	inline void readNoteFromFile(Deserializer& reader) { readFromFile(reader, MIDI_MESSAGE_NOTE); }
	inline void readCCFromFile(Deserializer& reader) { readFromFile(reader, MIDI_MESSAGE_CC); }
	inline void readChannelFromFile(Deserializer& reader) { readFromFile(reader, MIDI_MESSAGE_NONE); }

	void readMPEZone(Deserializer& reader);

	MIDICable* cable;
	// In addition to being set to channel 0 to 15, can also be MIDI_CHANNEL_MPE_LOWER_ZONE or
	// MIDI_CHANNEL_MPE_UPPER_ZONE. Channels 18-36 signify CCs on channels 1-16+MPE respectively. Check with the
	// constant IS_A_CC
	uint8_t channelOrZone;
	uint8_t noteOrCC;
};
