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

#include "din.h"
#include "drivers/uart/uart.h"
#include "gui/l10n/l10n.h"
#include "io/midi/midi_engine.h"
#include "libdeluge/midi_io.h"
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

void MIDICableDINPorts::sendMessage(MIDIMessage message) {
	midiEngine.sendSerialMidi(message);
}

size_t MIDICableDINPorts::sendBufferSpace() const {
	return deluge_midi_write_space(DELUGE_MIDI_DIN);
}

void MIDICableDINPorts::sendSysex(const uint8_t* data, int32_t len) {
	if (len < 3 || data[0] != 0xf0 || data[len - 1] != 0xf7) {
		return;
	}

	// NB: beware of MIDI_TX_BUFFER_SIZE
	deluge_midi_write(DELUGE_MIDI_DIN, data, len);
}

bool MIDICableDINPorts::wantsToOutputMIDIOnChannel(MIDIMessage message, int32_t filter) const {
	return message.isSystemMessage() || MIDICable::wantsToOutputMIDIOnChannel(message, filter);
}
