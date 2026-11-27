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

#include "serialization/schema/extras.h"
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::project {

/// @brief How many MIDI CC numbers are real controllers, 0 to 119: a MIDI instrument can label each, and a MIDI clip's
/// params number from them. 120 up are the channel-mode messages.
inline constexpr int32_t kNumRealCcNumbers = 120;

/// @name How a learned command's channel_or_zone encodes the message kind: 0–15 a note's channel, 16 and 17 the MPE
///       lower and upper zones, then the same eighteen again for a CC and once more for a program change.
/// @{
inline constexpr uint8_t kFirstCcChannel = 18;            ///< the first channel_or_zone encoding a CC
inline constexpr uint8_t kFirstProgramChangeChannel = 36; ///< the first channel_or_zone encoding a program change
/// @}

/// @brief Which MIDI device a command listens to, as a song names it: a fixed port, or a USB device the Deluge hosts,
/// known by its name and USB ids.
struct MidiDeviceRef {
	enum class Port : uint8_t {
		DIN,
		UPSTREAM_USB_1,
		UPSTREAM_USB_2,
		UPSTREAM_USB_3,
		HOSTED,
	};
	Port port{Port::DIN};
	std::string name;       ///< HOSTED only: the device's USB product name
	uint16_t vendor_id{0};  ///< HOSTED only
	uint16_t product_id{0}; ///< HOSTED only

	/// Keys a document held here that this firmware does not know, written back unchanged.
	serialization::schema::Extras extras;
	bool operator==(const MidiDeviceRef&) const = default;
};

/// @brief A learned MIDI command: a note or CC on a channel or MPE zone, optionally only from one device.
struct MidiCommand {
	/// The value of channel_or_zone and note_or_cc when nothing is learned.
	static constexpr uint8_t kNone = 255;
	/// A MIDI channel 0–15, an MPE zone (16 lower, 17 upper), a CC-encoded channel (from kFirstCcChannel), or kNone.
	/// A launch command is never a program change (from kFirstProgramChangeChannel).
	uint8_t channel_or_zone{kNone};
	uint8_t note_or_cc{kNone};
	/// The device it must come from when inputs are told apart by device; empty means any device.
	std::optional<MidiDeviceRef> device;

	[[nodiscard]] bool contains_something() const { return channel_or_zone != kNone; }

	/// Keys a document held here that this firmware does not know, written back unchanged.
	serialization::schema::Extras extras;
	bool operator==(const MidiCommand&) const = default;
};

} // namespace deluge::project
