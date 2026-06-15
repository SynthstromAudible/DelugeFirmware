/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include <cstdint>

namespace deluge::dsp::fixed {

/// @file
/// @brief Linear interpolation into a 1-D fixed-point lookup table — the generic, low-level cousin of the
/// band-limited @ref WindowedSincKernel. Stateless: the caller owns the table; this just samples it at a
/// fractional index. Mirrors the @ref windowed_sinc_kernel idiom — a class with methods and one shared
/// `inline constexpr` instance — rather than loose free functions.

/// @brief A 1-D fixed-point LUT sampled with linear interpolation between adjacent entries.
///
/// The table is indexed by a fixed-point @p input: its top @p table_size_log2 bits select the entry, the
/// remaining low bits are the sub-entry fraction used to blend toward the next entry. The interpolation weight
/// is a 15-bit fraction, so the returned value is the table value scaled by `2^15` (Q15 of the table's own
/// units) — callers typically `multiply_..._rshift32` or `>> 15` it back down.
class LinearTableInterpolator {
public:
	/// @brief Sample @p table at fractional index @p input.
	/// @param input Fixed-point index with @p input_fractional_bits fractional bits below the entry index.
	///        Must carry no bits above @p input_fractional_bits.
	/// @param input_fractional_bits Total bit-width of @p input (entry-index bits + fraction bits).
	/// @param table The lookup table; must hold at least `2^table_size_log2 + 1` entries (the `+1` is the
	///        upper neighbour the top entry blends toward).
	/// @param table_size_log2 log2 of the table's entry count (so the entry index is @p table_size_log2 bits).
	/// @return The interpolated value at Q15 of the table's units (`table_value * 2^15`).
	[[nodiscard]] int32_t interpolate(uint32_t input, int32_t input_fractional_bits, const uint16_t* table,
	                                  int32_t table_size_log2 = 8) const;

	/// @brief The inverse of @ref interpolate: find the @p input that would yield @p table_value_q15, by
	/// binary-searching the monotonic table. Clamps to the table's ends.
	/// @param table_value_q15 The target value at Q15 (i.e. `desired_table_value << 15`), matching what
	///        @ref interpolate returns.
	/// @param output_fractional_bits Bit-width of the returned index (its full fixed-point resolution).
	/// @param table The monotonic lookup table (ascending or descending); same layout as @ref interpolate.
	/// @param table_size_log2 log2 of the table's entry count.
	/// @return The fixed-point index, in `[0, 2^output_fractional_bits)`.
	[[nodiscard]] uint32_t inverse(int32_t table_value_q15, int32_t output_fractional_bits, const uint16_t* table,
	                               int32_t table_size_log2 = 8) const;
};

/// The shared stateless instance (matches @ref windowed_sinc_kernel).
inline constexpr LinearTableInterpolator linear_table_interpolator{};

/// @brief Curve lookup into the `decayTableSmall8` decay curve.
/// @param input Fixed-point position with @p input_fractional_bits fractional bits.
/// @return The interpolated decay value at Q15 (see @ref LinearTableInterpolator::interpolate).
int32_t getDecay8(uint32_t input, uint8_t input_fractional_bits);

/// @brief Curve lookup into the `decayTableSmall4` decay curve (a different curvature to @ref getDecay8).
/// @param input Fixed-point position with @p input_fractional_bits fractional bits.
/// @return The interpolated decay value at Q15 (see @ref LinearTableInterpolator::interpolate).
int32_t getDecay4(uint32_t input, uint8_t input_fractional_bits);

} // namespace deluge::dsp::fixed
