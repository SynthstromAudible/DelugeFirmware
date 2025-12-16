/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "io/midi/midi_device.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "io/debug/log.h"
#include "io/midi/midi_engine.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

MIDICable::MIDICable() {
	connectionFlags = 0;
	sendClock = true;
	receiveClock = true;
	defaultVelocityToLevel = 0; // Means none set.

	// These defaults for MPE are prescribed in the MPE standard. Wish we had the same for regular MIDI
	mpeZoneBendRanges[MPE_ZONE_LOWER_NUMBERED_FROM_0][BEND_RANGE_MAIN] = 2;
	mpeZoneBendRanges[MPE_ZONE_UPPER_NUMBERED_FROM_0][BEND_RANGE_MAIN] = 2;
	mpeZoneBendRanges[MPE_ZONE_LOWER_NUMBERED_FROM_0][BEND_RANGE_FINGER_LEVEL] = 48;
	mpeZoneBendRanges[MPE_ZONE_UPPER_NUMBERED_FROM_0][BEND_RANGE_FINGER_LEVEL] = 48;
}

void MIDICable::writeReferenceToFile(Serializer& writer, char const* tagName) {
	writer.writeOpeningTagBeginning(tagName);
	writeReferenceAttributesToFile(writer);
	writer.closeTag();
}

void MIDICable::dataEntryMessageReceived(ModelStack* modelStack, int32_t channel, int32_t msb) {

	// If it's pitch bend range...
	if (!inputChannels[channel].rpnMSB && !inputChannels[channel].rpnLSB) {

		int32_t whichBendRange = BEND_RANGE_MAIN; // Default

		int32_t channelOrZone = ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(channel);
		int32_t zone = channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;
		if (zone >= 0) { // If it *is* MPE-related...

			// If it's on the "main" channel
			if (ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isMasterChannel(channel)) {
setMPEBendRange:
				mpeZoneBendRanges[zone][whichBendRange] = msb;
			}

			// Or, member channel (in which case it should be applied for all member channels, according to the MPE
			// spec.
			else {
				whichBendRange = BEND_RANGE_FINGER_LEVEL;
				goto setMPEBendRange;
			}
		}

		// Or, if *not* MPE-related
		else {
			inputChannels[channel].bendRange = msb;
		}

		// Inform the Song
		modelStack->song->midiCableBendRangeUpdatedViaMessage(modelStack, *this, channelOrZone, whichBendRange, msb);
	}

	// Or if it's an MCM (MPE Configuration Message) to set up an MPE zone...
	else if (!inputChannels[channel].rpnMSB && inputChannels[channel].rpnLSB == 6) {
		if (msb < 16) {
			int32_t zone;

			// Master Channel of Lower zone
			if (ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel && channel == 0) {
				zone = MPE_ZONE_LOWER_NUMBERED_FROM_0;
				ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeLowerZoneLastMemberChannel = msb;
				ports[MIDI_DIRECTION_INPUT_TO_DELUGE]
				    .moveUpperZoneOutOfWayOfLowerZone(); // Move other zone out of the way if necessary (MPE spec says
				                                         // to do this).

resetBendRanges: // Have to reset pitch bend range for zone, according to MPE spec. Unless we just deactivated the MPE
                 // zone...
				if (msb) {
					mpeZoneBendRanges[zone][BEND_RANGE_MAIN] = 2;
					mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL] = 48;

					// Inform the Song about the changed bend ranges
					int32_t channelOrZone = zone + MIDI_CHANNEL_MPE_LOWER_ZONE;
					for (int32_t r = 0; r < 2; r++) {
						modelStack->song->midiCableBendRangeUpdatedViaMessage(modelStack, *this, channelOrZone, r,
						                                                      mpeZoneBendRanges[zone][r]);
					}
				}

				MIDIDeviceManager::recountSmallestMPEZones();
				MIDIDeviceManager::anyChangesToSave = true;

				// TODO: we're supposed to also ensure no notes left on by channels no longer in use...

				soundEditor.mpeZonesPotentiallyUpdated();
			}

			// Master Channel of Upper zone
			else if (ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel < 15 && channel == 15) {
				zone = MPE_ZONE_UPPER_NUMBERED_FROM_0;
				ports[MIDI_DIRECTION_INPUT_TO_DELUGE].mpeUpperZoneLastMemberChannel = 15 - msb;
				// Move other zone out of the way if necessary (MPE spec says to do this).
				ports[MIDI_DIRECTION_INPUT_TO_DELUGE].moveLowerZoneOutOfWayOfUpperZone();

				goto resetBendRanges;
			}
			// Any other channels "are invalid and should be ignored", according to MPE spec.
		}
	}
}

bool MIDICable::wantsToOutputMIDIOnChannel(MIDIMessage message, int32_t filter) const {
	auto lowerLastMember = ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel;
	auto upperLastMember = ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel;

	switch (filter) {
	case MIDI_CHANNEL_MPE_LOWER_ZONE:
		return lowerLastMember != 0;

	case MIDI_CHANNEL_MPE_UPPER_ZONE:
		return upperLastMember != 15;

	default:
		return (lowerLastMember == 0 || lowerLastMember < message.channel)
		       && (upperLastMember == 15 || upperLastMember > message.channel);
	}
}

void MIDICable::sendRPN(int32_t channel, int32_t rpnMSB, int32_t rpnLSB, int32_t valueMSB) {

	// Set the RPN number
	sendCC(channel, 0x64, rpnLSB);
	sendCC(channel, 0x65, rpnMSB);

	// Send the value
	sendCC(channel, 0x06, valueMSB);

	// Signal end of transmission by resetting RPN number to 0x7F (127)
	sendCC(channel, 0x64, 0x7F);
	sendCC(channel, 0x65, 0x7F);
}

void MIDICable::sendAllMCMs() {
	MIDIPort* port = &ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE];

	if (port->mpeLowerZoneLastMemberChannel != 0) {
		sendRPN(0, 0, 6, port->mpeLowerZoneLastMemberChannel);
	}

	if (port->mpeUpperZoneLastMemberChannel != 15) {
		sendRPN(15, 0, 6, 15 - port->mpeUpperZoneLastMemberChannel);
	}
}

bool MIDICable::worthWritingToFile() {
	return (ports[MIDI_DIRECTION_INPUT_TO_DELUGE].worthWritingToFile()
	        || ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].worthWritingToFile() || hasDefaultVelocityToLevelSet()
	        || !sendClock);
}

void MIDICable::writePorts(Serializer& writer) {
	ports[MIDI_DIRECTION_INPUT_TO_DELUGE].writeToFile(writer, "input");
	ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].writeToFile(writer, "output");
}

// Not to be called for MIDICableUSBHosted. readAHostedDeviceFromFile() handles that and needs to read the name and
// ids.
void MIDICable::readFromFile(Deserializer& reader) {
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "input")) {
			ports[MIDI_DIRECTION_INPUT_TO_DELUGE].readFromFile(reader, nullptr);
		}
		else if (!strcmp(tagName, "output")) {
			ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].readFromFile(reader, this);
		}
		else if (!strcmp(tagName, "defaultVolumeVelocitySensitivity")) {
			defaultVelocityToLevel = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "sendClock")) {
			sendClock = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "receiveClock")) {
			receiveClock = reader.readTagOrAttributeValueInt();
		}

		reader.exitTag();
	}
}

// These only go into SETTINGS/MIDICables.XML
void MIDICable::writeDefinitionAttributesToFile(Serializer& writer) {
	if (hasDefaultVelocityToLevelSet()) {
		writer.writeAttribute("defaultVolumeVelocitySensitivity", defaultVelocityToLevel);
	}
	writer.writeAttribute("sendClock", sendClock);
	writer.writeAttribute("receiveClock", receiveClock);
}

void MIDICable::writeToFile(Serializer& writer, char const* tagName) {
	writer.writeOpeningTagBeginning(tagName);
	writeReferenceAttributesToFile(writer);
	writeDefinitionAttributesToFile(writer);
	writer.writeOpeningTagEnd();
	writePorts(writer);
	writer.writeClosingTag(tagName);
}

int32_t MIDIPort::channelToZone(int32_t inputChannel) {
	if (mpeLowerZoneLastMemberChannel && mpeLowerZoneLastMemberChannel >= inputChannel) {
		return MIDI_CHANNEL_MPE_LOWER_ZONE;
	}
	if (mpeUpperZoneLastMemberChannel < 15 && mpeUpperZoneLastMemberChannel <= inputChannel) {
		return MIDI_CHANNEL_MPE_UPPER_ZONE;
	}
	return inputChannel;
}

bool MIDIPort::isMasterChannel(int32_t inputChannel) {
	if (mpeLowerZoneLastMemberChannel && inputChannel == 0) {
		return true;
	}
	if (mpeUpperZoneLastMemberChannel < 15 && inputChannel == 15) {
		return true;
	}
	return false;
}

void MIDIPort::moveUpperZoneOutOfWayOfLowerZone() {
	if (mpeLowerZoneLastMemberChannel >= 14) {
		mpeUpperZoneLastMemberChannel = 15;
	}
	else if (mpeLowerZoneLastMemberChannel >= 1) {
		if (mpeUpperZoneLastMemberChannel <= mpeLowerZoneLastMemberChannel) {
			mpeUpperZoneLastMemberChannel = mpeLowerZoneLastMemberChannel + 1;
		}
	}
}

void MIDIPort::moveLowerZoneOutOfWayOfUpperZone() {
	if (mpeUpperZoneLastMemberChannel <= 1) {
		mpeLowerZoneLastMemberChannel = 0;
	}
	else if (mpeUpperZoneLastMemberChannel <= 14) {
		if (mpeLowerZoneLastMemberChannel >= mpeUpperZoneLastMemberChannel) {
			mpeLowerZoneLastMemberChannel = mpeUpperZoneLastMemberChannel - 1;
		}
	}
}

void MIDIPort::writeToFile(Serializer& writer, char const* tagName) {

	int32_t numUpperMemberChannels = 15 - mpeUpperZoneLastMemberChannel;

	if (numUpperMemberChannels || mpeLowerZoneLastMemberChannel) {
		writer.writeOpeningTag(tagName);

		if (mpeLowerZoneLastMemberChannel) {
			writer.writeOpeningTagBeginning("mpeLowerZone");
			writer.writeAttribute("numMemberChannels", mpeLowerZoneLastMemberChannel);
			writer.closeTag();
		}
		if (numUpperMemberChannels) {
			writer.writeOpeningTagBeginning("mpeUpperZone");
			writer.writeAttribute("numMemberChannels", numUpperMemberChannels);
			writer.closeTag();
		}

		writer.writeClosingTag(tagName);
	}
}

void MIDIPort::readFromFile(Deserializer& reader, MIDICable* deviceToSendMCMsOn) {
	char const* tagName;
	bool sentMPEConfig = false;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "mpeLowerZone")) {

			char const* tagName;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "numMemberChannels")) {

					if (!mpeLowerZoneLastMemberChannel) { // If value was already set, then leave it - the user or an
						                                  // MCM might have changed it since the file was last read.
						int32_t newMPELowerZoneLastMemberChannel = reader.readTagOrAttributeValueInt();
						if (newMPELowerZoneLastMemberChannel >= 0 && newMPELowerZoneLastMemberChannel < 16) {
							mpeLowerZoneLastMemberChannel = newMPELowerZoneLastMemberChannel;
							moveLowerZoneOutOfWayOfUpperZone(); // Move self out of way of other - just in case user or
							                                    // MCM has set other and that's the important one they
							                                    // want now.
							if (deviceToSendMCMsOn) {
								deviceToSendMCMsOn->sendRPN(0, 0, 6, mpeLowerZoneLastMemberChannel);
								sentMPEConfig = true;
							}
						}
					}
				}

				reader.exitTag();
			}
		}
		else if (!strcmp(tagName, "mpeUpperZone")) {

			char const* tagName;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "numMemberChannels")) {
					// If value was already set, then leave it - the user or an MCM might have changed it since the file
					// was last read.
					if (mpeUpperZoneLastMemberChannel == 15) {
						int32_t numUpperMemberChannels = reader.readTagOrAttributeValueInt();
						if (numUpperMemberChannels >= 0 && numUpperMemberChannels < 16) {
							mpeUpperZoneLastMemberChannel = 15 - numUpperMemberChannels;
							moveUpperZoneOutOfWayOfLowerZone(); // Move self out of way of other - just in case user or
							                                    // MCM has set other and that's the important one they
							                                    // want now.
							if (deviceToSendMCMsOn) {
								deviceToSendMCMsOn->sendRPN(15, 0, 6, 15 - mpeUpperZoneLastMemberChannel);
								sentMPEConfig = true;
							}
						}
					}
				}

				reader.exitTag();
			}
		}

		reader.exitTag();
	}
}

bool MIDIPort::worthWritingToFile() {
	return (mpeLowerZoneLastMemberChannel || mpeUpperZoneLastMemberChannel != 15);
}
