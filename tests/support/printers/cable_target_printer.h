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

#include "synth/modulation/cable.h"

#include <format>
#include <ostream>

/// @file
/// @brief Prints a modulation cable's target in a failed CppSpec comparison, alone or as a field of a record that
///        holds one.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::synth {

/// @brief Prints a target as its packed word in hex: the param in the low byte, then up to three sources, `ff` for
///        none, e.g. `{word 0xffff0305}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const CableTarget& t) {
	return os << std::format("{{word {:#010x}}}", t.word);
}

} // namespace deluge::synth
