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

/// 4-bit MIDI channel-message status nibble values.
enum class MIDIStatusType : uint8_t {
	NoteOff = 0x08,
	NoteOn = 0x09,
	PolyphonicAftertouch = 0x0A,
	ControlChange = 0x0B,
	ProgramChange = 0x0C,
	ChannelAftertouch = 0x0D,
	PitchBend = 0x0E,
	System = 0x0F,
};

/// What a queued MIDI message is, which decides whether the output scheduler may merge or reorder it.
///
/// The default is the conservative one: a sender that says nothing gets verbatim, in-order delivery.
/// Only a sender that knows its messages are redundant opts into merging. That way a missed annotation
/// costs latency, never correctness.
enum class MIDIIntent : uint8_t {
	/// A discrete event. Queued verbatim and kept in order relative to other events; never coalesced,
	/// never reordered. RPN sequences, bank selects and program changes depend on this.
	Event,
	/// The current value of a continuous parameter, where a later value supersedes an earlier one.
	/// Eligible for coalescing and debt-based reordering. Automation and knob feedback use this.
	Continuous,
	/// A message that must stay ordered with the note stream: expression that initialises a note and
	/// must not be overtaken by it, or All Notes Off, which the notes queued after it must not overtake.
	NoteBound,
};

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
	/// How the output scheduler may treat this message. Defaults to the conservative Event, so the
	/// existing designated-initialiser constructors below need no changes.
	MIDIIntent intent = MIDIIntent::Event;

	[[gnu::always_inline]] [[nodiscard]] bool isSystemMessage() const {
		return statusType == static_cast<uint8_t>(MIDIStatusType::System);
	}

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

// Kept compact deliberately: MIDIMessage is passed by value along the whole send path. It is never
// stored in bulk - USB packs it to a uint32 and DIN converts it to raw bytes before queueing - so this
// bounds register pressure rather than any layout or interop requirement. Grew from 4 to 5 when `intent`
// was added; every field is a uint8_t, so alignment is 1 and there is no padding.
static_assert(sizeof(MIDIMessage) == 5);
