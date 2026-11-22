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

#include "project/arp_record.h"

#include <cstddef>
#include <format>
#include <ostream>

/// @file
/// @brief Prints an arp record in a failed CppSpec comparison, alone or as a field of a record that holds one.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints an arp record field by field, each byte as a number and the host values as a list, e.g.
///        `{mode 1, octave_mode 0, …, kit_arp 1, host_held 0x3, host_values [12 0 …]}`.
/// @param os The stream to print to.
/// @param a The record.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ArpRecord& a) {
	os << std::format("{{mode {}, octave_mode {}, note_mode {}, mpe_velocity {}, num_octaves {}, step_repeat {}, "
	                  "chord_type {}, sync_level {}, sync_type {}, randomizer_lock {}, kit_arp {}, host_held {:#x}, "
	                  "host_values [",
	                  a.mode, a.octave_mode, a.note_mode, a.mpe_velocity, a.num_octaves, a.step_repeat, a.chord_type,
	                  a.sync_level, a.sync_type, int{a.randomizer_lock}, int{a.kit_arp}, a.host_held);
	for (std::size_t i = 0; i < a.host_values.size(); ++i) {
		os << (i == 0 ? "" : " ") << a.host_values[i];
	}
	return os << "]}";
}

} // namespace deluge::project
