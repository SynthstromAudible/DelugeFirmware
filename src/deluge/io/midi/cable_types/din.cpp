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

#include "io/debug/log.h"
extern "C" {
#include "RZA1/uart/sio_char.h"
}

#include "din.h"
#include "drivers/uart/uart.h"
#include "gui/l10n/l10n.h"
#include "io/midi/midi_engine.h"
#include "storage/storage_manager.h"

void MIDICableDINPorts::writeReferenceAttributesToFile(Serializer& writer) {
	// Same line. Usually the user wouldn't have default velocity sensitivity set
	writer.writeAttribute("port", "din", false);
}

void MIDICableDINPorts::writeToFlash(uint8_t* memory) {
	*(uint16_t*)memory = VENDOR_ID_DIN;
}

char const* MIDICableDINPorts::getDisplayName() {
	return deluge::l10n::get(deluge::l10n::String::STRING_FOR_DIN_PORTS);
}

Error MIDICableDINPorts::sendMessage(MIDIMessage message) {
	uint8_t statusByte = message.channel | (message.statusType << 4);
	int32_t messageLength = bytesPerStatusMessage(statusByte);

	if (messageLength > sendBufferSpace()) {
		return Error::OUT_OF_BUFFER_SPACE;
	}

	bufferMIDIUart(statusByte);

	if (messageLength >= 2) {
		bufferMIDIUart(message.data1);

		if (messageLength == 3) {
			bufferMIDIUart(message.data2);
		}
	}

	return Error::NONE;
}

size_t MIDICableDINPorts::sendBufferSpace() const {
	return uartGetTxBufferSpace(UART_ITEM_MIDI);
}

Error MIDICableDINPorts::sendSysex(const uint8_t* data, int32_t len) {
	if (len < 3 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return Error::OUT_OF_BUFFER_SPACE;
	}
	if (len > sendBufferSpace()) {
		return Error::OUT_OF_BUFFER_SPACE;
	}

	// NB: beware of MIDI_TX_BUFFER_SIZE
	for (int32_t i = 0; i < len; i++) {
		bufferMIDIUart(data[i]);
	}

	return Error::NONE;
}

Error MIDICableDINPorts::onReceiveByte(uint32_t timestamp, uint8_t thisSerialByte) {
	// D_PRINTLN((uint32_t)thisSerialByte);
	// If this is a status byte, then we have to store it as the first byte.
	if (thisSerialByte & 0x80) {

		switch (thisSerialByte) {

		// If it's a realtime message, we have to obey it right now, separately from any other message it was
		// inserted into the middle of
		case 0xF8 ... 0xFF:
			midiEngine.midiMessageReceived(*this, thisSerialByte >> 4, thisSerialByte & 0x0F, 0, 0, &timestamp);
			return Error::NONE;

		// Or if it's a SysEx start...
		case 0xF0:
			currentlyReceivingSysex_ = true;
			D_PRINTLN("Sysex start");
			incomingSysexBuffer[0] = thisSerialByte;
			incomingSysexPos = 1;
			// numSerialMidiInput = 0; // This would throw away any running status stuff...
			return Error::NONE;
		}

		// If we didn't return for any of those, then it's just a regular old status message (or Sysex stop
		// message). All these will end any ongoing SysEx

		// If it was a Sysex stop, that's all we need to do
		if (thisSerialByte == 0xF7) {
			D_PRINTLN("Sysex end");
			if (currentlyReceivingSysex_) {
				currentlyReceivingSysex_ = false;
				if (incomingSysexPos < sizeof incomingSysexBuffer) {
					incomingSysexBuffer[incomingSysexPos++] = thisSerialByte;
					midiEngine.midiSysexReceived(*this, incomingSysexBuffer, incomingSysexPos);
				}
			}
			return Error::NONE;
		}

		currentlyReceivingSysex_ = false;
		currentByte_ = 0;
	}

	// If not a status byte...
	else {
		// If we're currently receiving a SysEx, don't throw it away
		if (currentlyReceivingSysex_) {
			// TODO: allocate a GMA buffer to some bigger size
			if (incomingSysexPos < sizeof incomingSysexBuffer) {
				incomingSysexBuffer[incomingSysexPos++] = thisSerialByte;
			}
			D_PRINTLN("Sysex:  %d", thisSerialByte);
			return Error::NONE;
		}

		// Ensure that we're not writing to the first byte of the buffer
		if (currentByte_ == 0) {
			return Error::NONE;
		}
	}

	messageBytes_[currentByte_] = thisSerialByte;
	currentByte_++;

	// If we've received the whole MIDI message, deal with it
	if (bytesPerStatusMessage(messageBytes_[0]) == currentByte_) {
		uint8_t type = (messageBytes_[0]) >> 4;
		uint8_t channel = messageBytes_[0] & 0x0F;

		midiEngine.midiMessageReceived(*this, type, channel, messageBytes_[1], messageBytes_[2], &timestamp);

		// If message was more than 1 byte long, and was a voice or mode message, then allow for running status
		if (currentByte_ > 1 && type != 0xF) {
			currentByte_ = 1;
		}
		else {
			currentByte_ = 0;
		}
	}

	return Error::NONE;
}

bool MIDICableDINPorts::wantsToOutputMIDIOnChannel(MIDIMessage message, int32_t filter) const {
	return message.isSystemMessage() || MIDICable::wantsToOutputMIDIOnChannel(message, filter);
}
