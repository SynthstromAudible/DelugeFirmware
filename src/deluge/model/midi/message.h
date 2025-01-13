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

#pragma once

#include <cinttypes>
#include <cstdlib>

/// Get the number of bytes associated with the provided status byte.
size_t bytesPerStatusMessage(uint8_t status);

/// Container for a MIDI status message.
///
/// See https://michd.me/jottings/midi-message-format-reference/ for a reference on the different status types and MIDI
/// encoding in general.
struct MIDIMessage {
	/// Status type. If 0xF, the channel represents the specific system function
	uint8_t statusType;
	/// Channel, or data field for system function
	uint8_t channel;
	/// Optional data byte 1
	uint8_t data1;
	/// Optional data byte 2
	uint8_t data2;

	[[gnu::always_inline]] [[nodiscard]] bool isSystemMessage() const { return statusType == 0x0f; }

	/// @name Constructors for certain types of message
	/// @{

	static MIDIMessage noteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
		return MIDIMessage{.statusType = 0b1000, .channel = channel, .data1 = note, .data2 = velocity};
	}

	static MIDIMessage noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
		return MIDIMessage{.statusType = 0b1001, .channel = channel, .data1 = note, .data2 = velocity};
	}

	static MIDIMessage polyphonicAftertouch(uint8_t channel, uint8_t note, uint8_t aftertouch) {
		return MIDIMessage{.statusType = 0b1010, .channel = channel, .data1 = note, .data2 = aftertouch};
	}

	static MIDIMessage cc(uint8_t channel, uint8_t cc, uint8_t value) {
		return MIDIMessage{.statusType = 0b1011, .channel = channel, .data1 = cc, .data2 = value};
	}

	static MIDIMessage programChange(uint8_t channel, uint8_t program) {
		return MIDIMessage{.statusType = 0b1100, .channel = channel, .data1 = program, .data2 = 0};
	}

	static MIDIMessage channelAftertouch(uint8_t channel, uint8_t aftertouch) {
		return MIDIMessage{.statusType = 0b1101, .channel = channel, .data1 = aftertouch, .data2 = 0};
	}

	/// Bend is 14 bits
	static MIDIMessage pitchBend(uint8_t channel, uint16_t bend) {
		return MIDIMessage{
		    .statusType = 0b1110,
		    .channel = channel,
		    .data1 = static_cast<uint8_t>(bend & 0x7f),
		    .data2 = static_cast<uint8_t>((bend >> 7) & 0x7f),
		};
	}

	static MIDIMessage realtimeClock() {
		return MIDIMessage{.statusType = 0x0F, .channel = 0x08, .data1 = 0, .data2 = 0};
	}

	static MIDIMessage realtimeStart() {
		return MIDIMessage{.statusType = 0x0F, .channel = 0x0A, .data1 = 0, .data2 = 0};
	}

	static MIDIMessage realtimeContinue() {
		return MIDIMessage{.statusType = 0x0F, .channel = 0x0B, .data1 = 0, .data2 = 0};
	}

	static MIDIMessage realtimeStop() {
		return MIDIMessage{.statusType = 0x0F, .channel = 0x0C, .data1 = 0, .data2 = 0};
	}

	static MIDIMessage systemPositionPointer(uint16_t position) {
		uint8_t positionPointerLSB = position & (uint32_t)0x7F;
		uint8_t positionPointerMSB = (position >> 7) & (uint32_t)0x7F;
		return MIDIMessage{
		    .statusType = 0x0F, .channel = 0x02, .data1 = positionPointerLSB, .data2 = positionPointerMSB};
	}

	/// @}
};

static_assert(sizeof(MIDIMessage) == 4);
