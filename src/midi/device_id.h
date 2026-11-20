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

/// @file
/// @brief A MIDI device as this session names it: a small handle the host gives each cable it has seen. It is not
///        saved (a song names a device by `project::MidiDeviceRef`); it is how events and ports name a device while
///        the firmware runs.

#include <cstdint>

namespace deluge::midi {

/// @brief One MIDI device (one cable), by its session handle. The default value names none.
struct DeviceId {
	uint16_t value{0}; ///< The handle; 0 names no device.

	/// @brief Whether this names a device.
	/// @return False for the default value.
	[[nodiscard]] constexpr explicit operator bool() const { return value != 0; }

	/// @brief Whether both name the same device.
	constexpr bool operator==(const DeviceId&) const = default;
};

/// @brief The DIN ports.
inline constexpr DeviceId kDin{1};
/// @brief The first of the upstream USB device ports (the computer's).
inline constexpr DeviceId kUpstreamUsb1{2};
/// @brief The second upstream USB port.
inline constexpr DeviceId kUpstreamUsb2{3};
/// @brief The third upstream USB port (sysex).
inline constexpr DeviceId kUpstreamUsb3{4};
/// @brief The first handle a hosted USB device takes; each one the host sees gets the next.
inline constexpr DeviceId kFirstHosted{5};

} // namespace deluge::midi
