/*
 * Copyright © 2026 Sean Ditny
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

#include "io/midi/midi_queue_policy.h"
#include <cstdint>

/// @brief Transport traits for USB: one packed USB-MIDI event per queue element.
///
/// The CC lane policy is identical for both transports apart from these four operations, so it is
/// written once against this interface rather than duplicated per transport.
struct UsbTransport {
	using Element = uint32_t;

	/// @brief Elements one queued channel-CC message occupies. One packed event holds a whole message.
	static constexpr uint16_t cc_span = 1;

	/// @brief True when this element is a channel CC and therefore a coalescing/scheduling candidate.
	static bool is_channel_cc(Element const* e) {
		uint8_t status = static_cast<uint8_t>((e[0] >> 8) & 0xFF);
		return MIDIQueueManager::is_channel_cc_status_byte(status);
	}
	/// @brief MIDI status byte (type and channel) of a channel-CC element.
	static uint8_t status(Element const* e) { return static_cast<uint8_t>((e[0] >> 8) & 0xFF); }
	/// @brief CC number of a channel-CC element.
	static uint8_t cc_number(Element const* e) { return static_cast<uint8_t>((e[0] >> 16) & 0xFF); }
	/// @brief Rewrites only the value byte, preserving cable/CIN, status and CC number.
	static void set_value(Element* e, uint8_t value) {
		e[0] = (e[0] & 0x00FFFFFFu) | (static_cast<uint32_t>(value) << 24);
	}
};

/// @brief Transport traits for DIN: one raw serial byte per queue element.
struct DinTransport {
	using Element = uint8_t;

	/// @brief Elements one queued channel-CC message occupies: status, CC number, value.
	static constexpr uint16_t cc_span = MIDIQueueManager::k_channel_cc_message_length;

	static bool is_channel_cc(Element const* e) { return MIDIQueueManager::is_channel_cc_status_byte(e[0]); }
	static uint8_t status(Element const* e) { return e[0]; }
	static uint8_t cc_number(Element const* e) { return e[1]; }
	static void set_value(Element* e, uint8_t value) { e[2] = value; }
};
