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

#include "midi/usb_event.h"

namespace deluge::midi {

namespace {
constexpr uint8_t kCinSongPositionPointer = 0x3;
constexpr uint8_t kStatusSongPositionPointer = 0xF2;
// Code index numbers below 0x8 are SysEx / system-common framing rather than a
// status nibble; the two we still decode as standalone messages are the 2- and
// 3-byte system-common ones, reported (as the legacy decoder did) as 0xF.
constexpr uint8_t kCinTwoByteSystemCommon = 0x2;
constexpr uint8_t kCinThreeByteSystemCommon = 0x3;
constexpr uint8_t kSystemStatusType = 0x0F;
constexpr uint8_t kFirstVoiceCin = 0x8;
} // namespace

UsbEventPacket pack_event(MIDIMessage message, uint8_t cable) {
	uint8_t status = static_cast<uint8_t>((message.channel & 0x0F) | (message.statusType << 4));
	uint8_t cin = (status == kStatusSongPositionPointer) ? kCinSongPositionPointer : message.statusType;
	return UsbEventPacket{
	    .header = static_cast<uint8_t>((cable << 4) | (cin & 0x0F)),
	    .status = status,
	    .data1 = message.data1,
	    .data2 = message.data2,
	};
}

std::optional<DecodedEvent> decode_event(UsbEventPacket packet) {
	uint8_t cin = packet.codeIndexNumber();

	uint8_t statusType = cin;
	if (cin < kFirstVoiceCin) {
		if (cin == kCinTwoByteSystemCommon || cin == kCinThreeByteSystemCommon) {
			statusType = kSystemStatusType;
		}
		else {
			// SysEx fragment, reserved, or single-byte/misc — not a standalone
			// message for the per-message decoder.
			return std::nullopt;
		}
	}

	// Data bytes must be 7-bit; a set high bit signals a corrupt frame.
	if ((packet.data1 & 0x80) != 0 || (packet.data2 & 0x80) != 0) {
		return std::nullopt;
	}

	return DecodedEvent{
	    .cable = packet.cable(),
	    .message =
	        MIDIMessage{
	            .statusType = statusType,
	            .channel = static_cast<uint8_t>(packet.status & 0x0F),
	            .data1 = packet.data1,
	            .data2 = packet.data2,
	        },
	};
}

} // namespace deluge::midi
