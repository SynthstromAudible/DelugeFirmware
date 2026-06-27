/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "midi/message.h"
#include <cstdint>
#include <optional>

/// USB-MIDI 1.0 event-packet (de)serialization — pure protocol, no hardware or
/// transport. The USB transport (BSP) moves opaque 4-byte groups; this library
/// turns them into and out of `MIDIMessage`s. Part of the dependency-free
/// `deluge_midi` library.
namespace deluge::midi {

/// A USB-MIDI 1.0 event packet: the 4-byte group as it sits in a USB bulk
/// buffer. Byte 0 is the *cable number* (high nibble) and *code index number*
/// (low nibble); bytes 1..3 are the MIDI status + data bytes.
struct UsbEventPacket {
	uint8_t header{}; ///< (cable << 4) | code index number
	uint8_t status{}; ///< MIDI status byte: (statusType << 4) | channel
	uint8_t data1{};
	uint8_t data2{};

	[[nodiscard]] constexpr uint8_t cable() const { return header >> 4; }
	[[nodiscard]] constexpr uint8_t codeIndexNumber() const { return header & 0x0F; }

	/// The little-endian 32-bit word as the SPI/DMA engine reads and writes it
	/// (byte 0 is the least-significant byte).
	[[nodiscard]] constexpr uint32_t word() const {
		return uint32_t{header} | (uint32_t{status} << 8) | (uint32_t{data1} << 16) | (uint32_t{data2} << 24);
	}

	[[nodiscard]] static constexpr UsbEventPacket fromWord(uint32_t w) {
		return UsbEventPacket{
		    .header = static_cast<uint8_t>(w),
		    .status = static_cast<uint8_t>(w >> 8),
		    .data1 = static_cast<uint8_t>(w >> 16),
		    .data2 = static_cast<uint8_t>(w >> 24),
		};
	}

	friend constexpr bool operator==(UsbEventPacket, UsbEventPacket) = default;
};
static_assert(sizeof(UsbEventPacket) == 4);

/// Pack a channel/system `message` into a USB-MIDI event packet on virtual
/// `cable`. The code index number follows the message's status, with the one
/// special case the spec calls for (a song-position-pointer uses CIN 0x3).
[[nodiscard]] UsbEventPacket pack_event(MIDIMessage message, uint8_t cable = 0);

/// A decoded USB-MIDI event: the message and the virtual cable it arrived on.
struct DecodedEvent {
	uint8_t cable;
	MIDIMessage message;

	friend constexpr bool operator==(DecodedEvent, DecodedEvent) = default;
};

/// Decode a USB-MIDI event packet into a message + cable. Returns `nullopt` for
/// packets a per-message decoder should *not* dispatch directly: SysEx fragments
/// (handled by a streaming parser), reserved/empty code index numbers, and
/// packets whose data bytes have the high bit set (a transmission error).
[[nodiscard]] std::optional<DecodedEvent> decode_event(UsbEventPacket packet);

} // namespace deluge::midi
