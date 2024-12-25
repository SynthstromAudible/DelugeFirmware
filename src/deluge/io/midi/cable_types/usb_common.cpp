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

void MIDICableUSB::connectedNow(int32_t midiDeviceNum) {
	connectionFlags |= (1 << midiDeviceNum);
	needsToSendMCMs = 2;
}

void MIDICableUSB::sendMCMsNowIfNeeded() {
	if (needsToSendMCMs) {
		needsToSendMCMs--;
		if (!needsToSendMCMs) {
			sendAllMCMs();
		}
	}
}

void MIDICableUSB::sendMessage(MIDIMessage message) {
	if (!connectionFlags) {
		return;
	}

	int32_t ip = 0;

	uint32_t fullMessage = setupUSBMessage(message);

	for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
		if (connectionFlags & (1 << d)) {
			ConnectedUSBMIDIDevice* connectedDevice = &connectedUSBMIDIDevices[ip][d];
			connectedDevice->bufferMessage(fullMessage);
		}
	}
}

size_t MIDICableUSB::sendBufferSpace() {
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

void MIDICableUSB::sendSysex(const uint8_t* data, int32_t len) {
	if (len < 6 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
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
		return;
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
}
