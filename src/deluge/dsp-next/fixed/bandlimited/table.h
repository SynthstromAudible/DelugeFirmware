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

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief A band-limited wave lookup table: the int16 samples plus the magnitude that
/// says how the phase indexes them.
///
/// `magnitude` is the table's resolution exponent: an oscillator picks the entry with
/// `index = phase >> (32 - magnitude)` (so the table holds 2^magnitude entries, plus a
/// trailing neighbour for interpolation). It is held explicitly rather than derived
/// from the span, so it matches the legacy `tableSizeMagnitude` exactly.
struct Table {
	std::span<const int16_t> samples;
	int32_t magnitude;
};

/// @brief Build a Table from a legacy raw table pointer + its magnitude, sizing the span
/// to the read extent (2^magnitude entries + the one interpolation neighbour). Bridges
/// the still-pointer-based legacy LUTs until they migrate into dsp-next as sized arrays.
[[nodiscard]] constexpr Table table_from_pointer(const int16_t* data, int32_t magnitude) {
	return Table{std::span<const int16_t>{data, (static_cast<std::size_t>(1) << magnitude) + 1}, magnitude};
}

} // namespace deluge::dsp::fixed
