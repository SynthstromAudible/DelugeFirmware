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

#include "scenes/bind/ports/midi_devices.h"

#include <ostream>

/// @file
/// @brief Prints a global command's or a follow input's learned binding in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, so the operator lives in the
/// type's own namespace. Every spec that compares the type includes this header: whether a type streams is decided
/// once per type in a binary.
namespace deluge::scenes::bind {

/// @brief Prints a binding as its device's handle (or any), its channel or zone (or none) and its number (or none),
/// e.g.
///        `{device 1, channel_or_zone 21, note_or_cc 42}`.
/// @param os The stream to print to.
/// @param b  The binding.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const LearnedBinding& b) {
	os << "{device ";
	if (b.device) {
		os << b.device->value;
	}
	else {
		os << "any";
	}
	os << ", channel_or_zone ";
	if (b.channel_or_zone) {
		os << static_cast<int>(*b.channel_or_zone);
	}
	else {
		os << "none";
	}
	os << ", note_or_cc ";
	if (b.note_or_cc) {
		os << static_cast<int>(*b.note_or_cc);
	}
	else {
		os << "none";
	}
	return os << "}";
}

} // namespace deluge::scenes::bind
