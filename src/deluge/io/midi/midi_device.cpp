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
#include "io/debug/print.h"
#include "io/midi/device_specific/specific_midi_device.h"
#include "io/midi/midi_engine.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "storage/storage_manager.h"
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

MIDIDevice::MIDIDevice() {
	connectionFlags = 0;
	sendClock = true;
	defaultVelocityToLevel = 0; // Means none set.
	memset(defaultInputMPEValuesPerMIDIChannel, 0, sizeof(defaultInputMPEValuesPerMIDIChannel));

	// These defaults for MPE are prescribed in the MPE standard. Wish we had the same for regular MIDI
	mpeZoneBendRanges[MPE_ZONE_LOWER_NUMBERED_FROM_0][BEND_RANGE_MAIN] = 2;
	mpeZoneBendRanges[MPE_ZONE_UPPER_NUMBERED_FROM_0][BEND_RANGE_MAIN] = 2;
	mpeZoneBendRanges[MPE_ZONE_LOWER_NUMBERED_FROM_0][BEND_RANGE_FINGER_LEVEL] = 48;
	mpeZoneBendRanges[MPE_ZONE_UPPER_NUMBERED_FROM_0][BEND_RANGE_FINGER_LEVEL] = 48;
}

void MIDIDevice::writeReferenceToFile(char const* tagName) {
	storageManager.writeOpeningTagBeginning(tagName);
	writeReferenceAttributesToFile();
	storageManager.closeTag();
}

void MIDIDevice::dataEntryMessageReceived(ModelStack* modelStack, int32_t channel, int32_t msb) {

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

			// Or, member channel (in which case it should be applied for all member channels, according to the MPE spec.
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
		modelStack->song->midiDeviceBendRangeUpdatedViaMessage(modelStack, this, channelOrZone, whichBendRange, msb);
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
				    .moveUpperZoneOutOfWayOfLowerZone(); // Move other zone out of the way if necessary (MPE spec says to do this).

resetBendRanges
    : // Have to reset pitch bend range for zone, according to MPE spec. Unless we just deactivated the MPE zone...
				if (msb) {
					mpeZoneBendRanges[zone][BEND_RANGE_MAIN] = 2;
					mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL] = 48;

					// Inform the Song about the changed bend ranges
					int32_t channelOrZone = zone + MIDI_CHANNEL_MPE_LOWER_ZONE;
					for (int32_t r = 0; r < 2; r++) {
						modelStack->song->midiDeviceBendRangeUpdatedViaMessage(modelStack, this, channelOrZone, r,
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
				ports[MIDI_DIRECTION_INPUT_TO_DELUGE]
				    .moveLowerZoneOutOfWayOfUpperZone(); // Move other zone out of the way if necessary (MPE spec says to do this).

				goto resetBendRanges;
			}
			// Any other channels "are invalid and should be ignored", according to MPE spec.
		}
	}
}

bool MIDIDevice::wantsToOutputMIDIOnChannel(int32_t channel, int32_t filter) {
	switch (filter) {
	case MIDI_CHANNEL_MPE_LOWER_ZONE:
		return (ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel != 0);

	case MIDI_CHANNEL_MPE_UPPER_ZONE:
		return (ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel != 15);

	default:
		return (ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel == 0
		        || ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeLowerZoneLastMemberChannel < channel)
		       && (ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel == 15
		           || ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].mpeUpperZoneLastMemberChannel > channel);
	}
}

void MIDIDevice::sendRPN(int32_t channel, int32_t rpnMSB, int32_t rpnLSB, int32_t valueMSB) {

	// Set the RPN number
	sendCC(channel, 0x64, rpnLSB);
	sendCC(channel, 0x65, rpnMSB);

	// Send the value
	sendCC(channel, 0x06, valueMSB);

	// Signal end of transmission by resetting RPN number to 0x7F (127)
	sendCC(channel, 0x64, 0x7F);
	sendCC(channel, 0x65, 0x7F);
}

void MIDIDevice::sendAllMCMs() {
	MIDIPort* port = &ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE];

	if (port->mpeLowerZoneLastMemberChannel != 0) {
		sendRPN(0, 0, 6, port->mpeLowerZoneLastMemberChannel);
	}

	if (port->mpeUpperZoneLastMemberChannel != 15) {
		sendRPN(15, 0, 6, 15 - port->mpeUpperZoneLastMemberChannel);
	}
}

bool MIDIDevice::worthWritingToFile() {
	return (ports[MIDI_DIRECTION_INPUT_TO_DELUGE].worthWritingToFile()
	        || ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].worthWritingToFile() || hasDefaultVelocityToLevelSet()
	        || !sendClock);
}

void MIDIDevice::writePorts() {
	ports[MIDI_DIRECTION_INPUT_TO_DELUGE].writeToFile("input");
	ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].writeToFile("output");
}

// Not to be called for MIDIDeviceUSBHosted. readAHostedDeviceFromFile() handles that and needs to read the name and ids.
void MIDIDevice::readFromFile() {
	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "input")) {
			ports[MIDI_DIRECTION_INPUT_TO_DELUGE].readFromFile(NULL);
		}
		else if (!strcmp(tagName, "output")) {
			ports[MIDI_DIRECTION_OUTPUT_FROM_DELUGE].readFromFile(this);
		}
		else if (!strcmp(tagName, "defaultVolumeVelocitySensitivity")) {
			defaultVelocityToLevel = storageManager.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "sendClock")) {
			sendClock = storageManager.readTagOrAttributeValueInt();
		}

		storageManager.exitTag();
	}
}

void MIDIDevice::writeDefinitionAttributesToFile() { // These only go into MIDIDEVICES.XML.
	if (hasDefaultVelocityToLevelSet()) {
		storageManager.writeAttribute("defaultVolumeVelocitySensitivity", defaultVelocityToLevel);
	}
	storageManager.writeAttribute("sendClock", sendClock);
}

void MIDIDevice::writeToFile(char const* tagName) {
	storageManager.writeOpeningTagBeginning(tagName);
	writeReferenceAttributesToFile();
	writeDefinitionAttributesToFile();
	storageManager.writeOpeningTagEnd();
	writePorts();
	storageManager.writeClosingTag(tagName);
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

void MIDIPort::writeToFile(char const* tagName) {

	int32_t numUpperMemberChannels = 15 - mpeUpperZoneLastMemberChannel;

	if (numUpperMemberChannels || mpeLowerZoneLastMemberChannel) {
		storageManager.writeOpeningTag(tagName);

		if (mpeLowerZoneLastMemberChannel) {
			storageManager.writeOpeningTagBeginning("mpeLowerZone");
			storageManager.writeAttribute("numMemberChannels", mpeLowerZoneLastMemberChannel);
			storageManager.closeTag();
		}
		if (numUpperMemberChannels) {
			storageManager.writeOpeningTagBeginning("mpeUpperZone");
			storageManager.writeAttribute("numMemberChannels", numUpperMemberChannels);
			storageManager.closeTag();
		}

		storageManager.writeClosingTag(tagName);
	}
}

void MIDIPort::readFromFile(MIDIDevice* deviceToSendMCMsOn) {
	char const* tagName;
	bool sentMPEConfig = false;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "mpeLowerZone")) {

			char const* tagName;
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "numMemberChannels")) {

					if (!mpeLowerZoneLastMemberChannel) { // If value was already set, then leave it - the user or an MCM might have changed it since the file was last read.
						int32_t newMPELowerZoneLastMemberChannel = storageManager.readTagOrAttributeValueInt();
						if (newMPELowerZoneLastMemberChannel >= 0 && newMPELowerZoneLastMemberChannel < 16) {
							mpeLowerZoneLastMemberChannel = newMPELowerZoneLastMemberChannel;
							moveLowerZoneOutOfWayOfUpperZone(); // Move self out of way of other - just in case user or MCM has set other and that's the important one they want now.
							if (deviceToSendMCMsOn) {
								deviceToSendMCMsOn->sendRPN(0, 0, 6, mpeLowerZoneLastMemberChannel);
								sentMPEConfig = true;
							}
						}
					}
				}

				storageManager.exitTag();
			}
		}
		else if (!strcmp(tagName, "mpeUpperZone")) {

			char const* tagName;
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "numMemberChannels")) {

					if (mpeUpperZoneLastMemberChannel
					    == 15) { // If value was already set, then leave it - the user or an MCM might have changed it since the file was last read.
						int32_t numUpperMemberChannels = storageManager.readTagOrAttributeValueInt();
						if (numUpperMemberChannels >= 0 && numUpperMemberChannels < 16) {
							mpeUpperZoneLastMemberChannel = 15 - numUpperMemberChannels;
							moveUpperZoneOutOfWayOfLowerZone(); // Move self out of way of other - just in case user or MCM has set other and that's the important one they want now.
							if (deviceToSendMCMsOn) {
								deviceToSendMCMsOn->sendRPN(15, 0, 6, 15 - mpeUpperZoneLastMemberChannel);
								sentMPEConfig = true;
							}
						}
					}
				}

				storageManager.exitTag();
			}
		}

		storageManager.exitTag();
	}
}

bool MIDIPort::worthWritingToFile() {
	return (mpeLowerZoneLastMemberChannel || mpeUpperZoneLastMemberChannel != 15);
}

void MIDIDeviceUSB::connectedNow(int32_t midiDeviceNum) {
	connectionFlags |= (1 << midiDeviceNum);
	needsToSendMCMs = 2;
}

void MIDIDeviceUSB::sendMCMsNowIfNeeded() {
	if (needsToSendMCMs) {
		needsToSendMCMs--;
		if (!needsToSendMCMs) {
			sendAllMCMs();
		}
	}
}

void MIDIDeviceUSB::sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
	if (!connectionFlags) {
		return;
	}

	int32_t ip = 0;

	uint32_t fullMessage = setupUSBMessage(statusType, channel, data1, data2);

	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
			connectedDevice->bufferMessage(fullMessage);
		}
	}
}

int32_t MIDIDeviceUSB::sendBufferSpace() {
	int32_t ip = 0;
	ConnectedUSBMIDIDevice* connectedDevice = NULL;

	// find the connected device for this specific device. Note that virtual
	// port number is specified as part of the message, implemented below.
	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			connectedDevice = &connectedUSBMIDIDevices[ip][d];
			break;
		}
	}

	if (!connectedDevice) {
		return 0;
	}

	return connectedDevice->sendBufferSpace();
}

void MIDIDeviceUSB::sendSysex(uint8_t* data, int32_t len) {
	if (len < 3 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
	}

	int32_t ip = 0;
	ConnectedUSBMIDIDevice* connectedDevice = NULL;

	// find the connected device for this specific device. Note that virtual
	// port number is specified as part of the message, implemented below.
	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			connectedDevice = &connectedUSBMIDIDevices[ip][d];
			break;
		}
	}

	if (!connectedDevice) {
		return;
	}

	int32_t pos = 0;
	while (pos < len) {
		int32_t status, byte0 = 0, byte1 = 0, byte2 = 0;
		byte0 = data[pos];
		if (len - pos > 3) {
			status = 0x4; // sysex start or continue
			byte1 = data[pos + 1];
			byte2 = data[pos + 2];
			pos += 3;
		}
		else {
			status = 0x4 + (len - pos); // sysex end with N bytes
			if ((len - pos) > 1) {
				byte1 = data[pos + 1];
			}
			if ((len - pos) > 2) {
				byte2 = data[pos + 2];
			}
			pos = len;
		}
		status |= (portNumber << 4);
		uint32_t packed = ((uint32_t)byte2 << 24) | ((uint32_t)byte1 << 16) | ((uint32_t)byte0 << 8) | status;
		connectedDevice->bufferMessage(packed);
	}
}

void MIDIDeviceUSBHosted::writeReferenceAttributesToFile() {
	storageManager.writeAttribute("name", name.get());
	storageManager.writeAttributeHex("vendorId", vendorId, 4);
	storageManager.writeAttributeHex("productId", productId, 4);
}

void MIDIDeviceUSBHosted::writeToFlash(uint8_t* memory) {
	*(uint16_t*)memory = vendorId;
	*(uint16_t*)(memory + 2) = productId;
}

char const* MIDIDeviceUSBHosted::getDisplayName() {
	return name.get();
}

void MIDIDeviceUSBHosted::callHook(Hook hook) {
	switch (hook) {
	case Hook::HOOK_ON_CONNECTED:
		hookOnConnected();
		break;
	case Hook::HOOK_ON_CHANGE_ROOT_NOTE:
		hookOnChangeRootNote();
		break;
	case Hook::HOOK_ON_CHANGE_SCALE:
		hookOnChangeScale();
		break;
	case Hook::HOOK_ON_ENTER_SCALE_MODE:
		hookOnEnterScaleMode();
		break;
	case Hook::HOOK_ON_EXIT_SCALE_MODE:
		hookOnExitScaleMode();
		break;
	case Hook::HOOK_ON_MIDI_LEARN:
		hookOnMIDILearn();
		break;
	case Hook::HOOK_ON_RECALCULATE_COLOUR:
		hookOnRecalculateColour();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_ARRANGER_VIEW:
		hookOnTransitionToArrangerView();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_CLIP_VIEW:
		hookOnTransitionToClipView();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW:
		hookOnTransitionToSessionView();
		break;
	case Hook::HOOK_ON_WRITE_HOSTED_DEVICE_TO_FILE:
		hookOnWriteHostedDeviceToFile();
		break;
	default:
		break;
	}
}

void MIDIDeviceUSBUpstream::writeReferenceAttributesToFile() {
	storageManager.writeAttribute(
	    "port", portNumber ? "upstreamUSB2" : "upstreamUSB",
	    false); // Same line. Usually the user wouldn't have default velocity sensitivity set for their computer.
}

void MIDIDeviceUSBUpstream::writeToFlash(uint8_t* memory) {
	Debug::print("writing to flash port ");
	Debug::print(portNumber);
	Debug::print(" into ");
	*(uint16_t*)memory = portNumber ? VENDOR_ID_UPSTREAM_USB2 : VENDOR_ID_UPSTREAM_USB;
	Debug::println(*memory);
}

char const* MIDIDeviceUSBUpstream::getDisplayName() {
	switch (portNumber) {
	case 0:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_1);
	case 1:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_2);
	case 2:
		return deluge::l10n::get(deluge::l10n::String::STRING_FOR_UPSTREAM_USB_PORT_3_SYSEX);
	default:
		return "";
	}
}

void MIDIDeviceDINPorts::writeReferenceAttributesToFile() {
	storageManager.writeAttribute("port", "din",
	                              false); // Same line. Usually the user wouldn't have default velocity sensitivity set
}

void MIDIDeviceDINPorts::writeToFlash(uint8_t* memory) {
	*(uint16_t*)memory = VENDOR_ID_DIN;
}

char const* MIDIDeviceDINPorts::getDisplayName() {
	return deluge::l10n::get(deluge::l10n::String::STRING_FOR_DIN_PORTS);
}

void MIDIDeviceDINPorts::sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
	midiEngine.sendSerialMidi(statusType, channel, data1, data2);
}

int32_t MIDIDeviceDINPorts::sendBufferSpace() {
	return uartGetTxBufferSpace(UART_ITEM_MIDI);
}

void MIDIDeviceDINPorts::sendSysex(uint8_t* data, int32_t len) {
	if (len < 3 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
	}

	// NB: beware of MIDI_TX_BUFFER_SIZE
	for (int32_t i = 0; i < len; i++) {
		bufferMIDIUart(data[i]);
	}
}

void MIDIDeviceLoopback::writeReferenceAttributesToFile() {
	storageManager.writeAttribute("port", "loopbackMidi", false);
}

void MIDIDeviceLoopback::writeToFlash(uint8_t* memory) {
	*(uint16_t*)memory = VENDOR_ID_LOOPBACK;
}

char const* MIDIDeviceLoopback::getDisplayName() {
	return deluge::l10n::get(deluge::l10n::String::STRING_FOR_LOOPBACK);
}

void MIDIDeviceLoopback::sendMessage(uint8_t statusType, uint8_t channel, uint8_t data1, uint8_t data2) {
}

int32_t MIDIDeviceLoopback::sendBufferSpace() {
	return 0;
}

void MIDIDeviceLoopback::sendSysex(uint8_t* data, int32_t len) {
}
