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

extern "C" {
#include "RZA1/uart/sio_char.h"
}

#include "din.h"
#include "drivers/uart/uart.h"
#include "gui/l10n/l10n.h"
#include "io/midi/midi_engine.h"
#include "storage/storage_manager.h"

void MIDIDeviceDINPorts::writeReferenceAttributesToFile(Serializer& writer) {
	// Same line. Usually the user wouldn't have default velocity sensitivity set
	writer.writeAttribute("port", "din", false);
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

void MIDIDeviceDINPorts::sendSysex(const uint8_t* data, int32_t len) {
	if (len < 3 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
	}

	// NB: beware of MIDI_TX_BUFFER_SIZE
	for (int32_t i = 0; i < len; i++) {
		bufferMIDIUart(data[i]);
	}
}
