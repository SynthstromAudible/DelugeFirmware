/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "midi/usb_event.h"

#include <format>
#include <ostream>

/// @file
/// @brief Prints a MIDI message, a USB-MIDI event packet and a decoded event in a failed CppSpec comparison, alone or
///        as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.

/// @brief Prints a message field by field, e.g. `{status_type 9, channel 5, data1 60, data2 100}`.
/// @param os The stream to print to.
/// @param m The message.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MIDIMessage& m) {
	return os << std::format("{{status_type {}, channel {}, data1 {}, data2 {}}}", m.statusType, m.channel, m.data1,
	                         m.data2);
}

namespace deluge::midi {

/// @brief Prints a packet's four bytes in hex, e.g. `{header 0x19, status 0x95, data1 0x3c, data2 0x64}`.
/// @param os The stream to print to.
/// @param p The packet.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const UsbEventPacket& p) {
	return os << std::format("{{header {:#04x}, status {:#04x}, data1 {:#04x}, data2 {:#04x}}}", p.header, p.status,
	                         p.data1, p.data2);
}

/// @brief Prints a decoded event as its cable and its message, e.g. `{cable 1, message {status_type 9, …}}`.
/// @param os The stream to print to.
/// @param e The event.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DecodedEvent& e) {
	return os << "{cable " << int{e.cable} << ", message " << e.message << "}";
}

} // namespace deluge::midi
