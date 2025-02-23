/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "usb_common.h"
#include "io/midi/midi_engine.h"

void MIDICableUSB::checkIncomingSysex(uint8_t const* msg, [[maybe_unused]] int32_t ip, [[maybe_unused]] int32_t d) {
	uint8_t status_type = msg[0] & 15;
	int32_t to_read = 0;
	bool will_end = false;
	if (status_type == 0x4) {
		// sysex start or continue
		if (msg[1] == 0xf0) {
			this->incomingSysexPos = 0;
		}
		to_read = 3;
	}
	else if (status_type >= 0x5 && status_type <= 0x7) {
		to_read = status_type - 0x4; // read between 1-3 bytes
		will_end = true;
	}

	for (int32_t i = 0; i < to_read; i++) {
		if (this->incomingSysexPos >= sizeof(this->incomingSysexBuffer)) {
			// TODO: allocate a GMA buffer to some bigger size
			this->incomingSysexPos = 0;
			return; // bail out
		}
		this->incomingSysexBuffer[this->incomingSysexPos++] = msg[i + 1];
	}

	if (will_end) {
		if (this->incomingSysexBuffer[0] == 0xf0) {
			midiEngine.midiSysexReceived(*this, this->incomingSysexBuffer, this->incomingSysexPos);
		}
		this->incomingSysexPos = 0;
	}
}

void MIDICableUSB::connectedNow(int32_t midiDeviceNum) {
	connectionFlags |= (1 << midiDeviceNum);
	needsToSendMCMs = 2;
}

void MIDICableUSB::sendMCMsNowIfNeeded() {
	if (needsToSendMCMs != 0u) {
		needsToSendMCMs--;
		if (needsToSendMCMs == 0u) {
			sendAllMCMs();
		}
	}
}

static uint32_t setupUSBMessage(MIDIMessage message) {
	// format message per USB midi spec on virtual cable 0
	uint8_t cin;
	uint8_t first_byte = (message.channel & 15) | (message.statusType << 4);

	switch (first_byte) {
	case 0xF2: // Position pointer
		cin = 0x03;
		break;

	default:
		cin = message.statusType; // Good for voice commands
		break;
	}

	return ((uint32_t)message.data2 << 24) | ((uint32_t)message.data1 << 16) | ((uint32_t)first_byte << 8) | cin;
}

Error MIDICableUSB::sendMessage(MIDIMessage message) {
	if (!connectionFlags) {
		return Error::NONE;
	}

	int32_t ip = 0;

	uint32_t fullMessage = setupUSBMessage(message);

	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
			if (connectedDevice->canHaveMIDISent) {
				uint32_t channeledMessage = fullMessage | (portNumber << 4);
				connectedDevice->bufferMessage(channeledMessage);
			}
		}
	}

	return Error::NONE;
}

size_t MIDICableUSB::sendBufferSpace() const {
	int32_t ip = 0;
	ConnectedUSBMIDIDevice* connectedDevice = nullptr;

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

extern bool developerSysexCodeReceived;

Error MIDICableUSB::sendSysex(const uint8_t* data, int32_t len) {
	if (len < 6 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return Error::INVALID_SYSEX_FORMAT;
	}

	if (len > sendBufferSpace()) {
		return Error::OUT_OF_BUFFER_SPACE;
	}

	int32_t ip = 0;
	ConnectedUSBMIDIDevice* connectedDevice = nullptr;

	// find the connected device for this specific device. Note that virtual
	// port number is specified as part of the message, implemented below.
	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			connectedDevice = &connectedUSBMIDIDevices[ip][d];
			break;
		}
	}

	if (!connectedDevice) {
		return Error::NONE;
	}

	int32_t pos = 0;

	// While we are standardizing on using the 4 byte Synthstrom Deluge in our messages, it is possible that client
	// programs have not been updated yet, so if we get a SysEx request with 0x7D as the ID, we respond in-kind. This
	// involves skipping the the first 5 bytes and sending 0xF0, 0x7D instead. Since the USB driver batches things in 3
	// byte groups, we must include the first byte after the address in that first group.
	if (developerSysexCodeReceived && (data[1] != 0x7D)) {
		// Since the message ends with 0xF7, we can assume that data[5] does exist.
		// fake 0xF0, 0x7D, data[5] for first send
		uint32_t packed = ((uint32_t)data[5] << 24) | 0x007DF004 | (portNumber << 4);
		connectedDevice->bufferMessage(packed);
		pos = 6;
	}

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

	return Error::NONE;
}

bool MIDICableUSB::wantsToOutputMIDIOnChannel(MIDIMessage message, int32_t filter) const {
	if (message.isSystemMessage()) {
		return sendClock;
	}
	else {
		return MIDICable::wantsToOutputMIDIOnChannel(message, filter);
	}
}
