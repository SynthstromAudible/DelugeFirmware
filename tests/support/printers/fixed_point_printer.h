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

#include "dsp/core/fixed_point.h"

#include <cmath>
#include <format>
#include <ostream>

/// @file
/// @brief Prints fixed-point values in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operators live in the values' own namespace so that lookup finds them from inside CppSpec.
/// Every spec that compares these types includes this header: whether a type streams is decided once per type in a
/// binary, so a spec linked with others that print the same type must agree with them.
namespace deluge::dsp {

/// @brief Prints a fixed-point value as its raw word and the real value it stands for, e.g. `1073741824 (0.5)`.
/// @param os The stream to print to.
/// @param v The value: a raw word in format Q(bits - F).F, real value `raw * 2^-F`.
/// @return @p os.
template <class S, int F>
std::ostream& operator<<(std::ostream& os, Fixed<S, F> v) {
	return os << std::format("{} ({})", v.raw(), std::ldexp(static_cast<double>(v.raw()), -F));
}

/// @brief Prints a packed pair of 16-bit fixed-point halves as its 32-bit word in hex, e.g. `0x0003fffe`: the high
///        half is the first four digits, the low half the last four.
/// @param os The stream to print to.
/// @param v The packed pair.
/// @return @p os.
template <int F>
std::ostream& operator<<(std::ostream& os, Q16x2<F> v) {
	return os << std::format("0x{:08x}", v.raw());
}

} // namespace deluge::dsp
