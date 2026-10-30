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

#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

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
	/// @brief The blend's full weight, 1.0 at Q15: an entry read at a blend fraction of 0 is scaled by it.
	static constexpr int32_t kFullWeight = int32_t{1} << 15;

	/// @brief Sample @p table at fractional index @p input.
	/// @param input Fixed-point index with @p input_fractional_bits fractional bits below the entry index.
	///        Must carry no bits above @p input_fractional_bits.
	/// @param input_fractional_bits Total bit-width of @p input (entry-index bits + fraction bits).
	/// @param table The lookup table; must hold at least `2^table_size_log2 + 1` entries (the `+1` is the
	///        upper neighbour the top entry blends toward).
	/// @param table_size_log2 log2 of the table's entry count (so the entry index is @p table_size_log2 bits).
	/// @return The interpolated value at Q15 of the table's units (`table_value * 2^15`).
	[[nodiscard, gnu::always_inline]] int32_t interpolate(uint32_t input, int32_t input_fractional_bits,
	                                                      const uint16_t* table, int32_t table_size_log2 = 8) const {
		int32_t index = input >> (input_fractional_bits - table_size_log2);
		int32_t value1 = table[index];
		int32_t value2 = table[index + 1];

		// The blend fraction is a 15-bit weight, so line up `input`'s sub-entry bits into the bottom 15.
		int32_t shift = input_fractional_bits - 15 - table_size_log2;
		uint32_t fraction = (shift >= 0) ? (input >> shift) : (input << -shift);

		int32_t strength2 = fraction & (kFullWeight - 1); // weight toward the upper neighbour (Q15)
		int32_t strength1 = kFullWeight - strength2;      // and the remainder toward the lower
		return value1 * strength1 + value2 * strength2;
	}

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

	/// @brief @ref interpolate for a **signed** (`int16_t`) table, with a full 16-bit blend weight.
	///
	/// Kept inline (header) because the sine LUT it backs (@ref SineTable) is evaluated per-sample in the
	/// oscillator-sync seam crossfade, a hot path that needs it inlined.
	/// @param input Fixed-point index with @p input_fractional_bits fractional bits.
	/// @param input_fractional_bits Total bit-width of @p input.
	/// @param table The signed lookup table; needs `2^table_size_log2 + 1` entries.
	/// @param table_size_log2 log2 of the table's entry count.
	/// @return The interpolated value at Q16 of the table's units (`table_value * 2^16`).
	[[nodiscard, gnu::always_inline]] int32_t interpolateSigned(uint32_t input, int32_t input_fractional_bits,
	                                                            const int16_t* table,
	                                                            int32_t table_size_log2 = 8) const {
		int32_t index = input >> (input_fractional_bits - table_size_log2);

		// The signed blend uses a full 16-bit weight (vs interpolate()'s 15-bit), so line up `input`'s
		// sub-entry bits into the bottom 16.
		int32_t shift = input_fractional_bits - 16 - table_size_log2;
		uint32_t fraction = (shift >= 0) ? (input >> shift) : (input << -shift);

		int32_t strength2 = fraction & 0xFFFF;   // weight toward the upper neighbour (Q16)
		int32_t strength1 = 0x10000 - strength2; // and the remainder toward the lower
		return static_cast<int32_t>(table[index]) * strength1 + static_cast<int32_t>(table[index + 1]) * strength2;
	}
};

/// The shared stateless instance (matches @ref windowed_sinc_kernel).
inline constexpr LinearTableInterpolator linear_table_interpolator{};

/// @brief A 2-D signed fixed-point LUT, sampled with linear interpolation along each row and then between the two
/// rows the second index falls between.
///
/// The table is a built-in `int16_t[R][W]` array known at compile time: `R = 2^m + 1` rows, each `W = 2^n + 1`
/// entries wide (the `+1`s are the upper neighbours). Its type gives both log2 sizes and the row stride as constants,
/// so the per-sample path is subscripts and arithmetic with no calls. Each row is sampled as
/// @ref LinearTableInterpolator::interpolateSigned does, through a pointer to that row, so every read stays inside one
/// row; the two row values are then blended by the second index's sub-row fraction at Q31, so the result is the
/// table's units scaled by `2^16` (at most ±2^30 for a table of ±2^14 entries).
class BilinearTableInterpolator {
public:
	/// @brief Sample @p Table at fractional indices @p input_x (along a row) and @p input_y (across rows).
	/// @tparam Table The table: a static `const int16_t[R][W]` with R − 1 and W − 1 powers of two.
	/// @param input_x Fixed-point position along a row, @p x_bits wide.
	/// @param input_y Fixed-point position across the rows, @p y_bits wide.
	/// @param x_bits The width of @p input_x: log2(W − 1) index bits above the fraction.
	/// @param y_bits The width of @p input_y: log2(R − 1) index bits above the fraction.
	/// @return The blend, at Q16 of the table's units.
	template <const auto& Table>
	[[nodiscard, gnu::always_inline]] int32_t interpolateSigned(uint32_t input_x, uint32_t input_y, int32_t x_bits,
	                                                            int32_t y_bits) const {
		using TableType = std::remove_cvref_t<decltype(Table)>;
		static_assert(std::rank_v<TableType> == 2, "the table is a built-in two-dimensional array");
		static_assert(std::is_same_v<std::remove_all_extents_t<TableType>, int16_t>, "the table holds int16_t");
		constexpr std::size_t kRows = std::extent_v<TableType, 0>;
		constexpr std::size_t kWidth = std::extent_v<TableType, 1>;
		static_assert(std::has_single_bit(kRows - 1) && std::has_single_bit(kWidth - 1),
		              "each axis is a power of two plus its upper neighbour");
		constexpr auto row_size_log2 = static_cast<int32_t>(std::bit_width(kWidth - 1) - 1);
		constexpr auto rows_log2 = static_cast<int32_t>(std::bit_width(kRows - 1) - 1);

		const uint32_t row = input_y >> (y_bits - rows_log2);
		const int32_t lower = linear_table_interpolator.interpolateSigned(input_x, x_bits, Table[row], row_size_log2);
		const int32_t upper =
		    linear_table_interpolator.interpolateSigned(input_x, x_bits, Table[row + 1], row_size_log2);

		// The sub-row fraction at Q31: the bits of `input_y` below the row index.
		const int32_t shift = 31 + rows_log2 - y_bits;
		const uint32_t toward_upper = (shift >= 0 ? (input_y << shift) : (input_y >> -shift)) & 2147483647u;
		const uint32_t toward_lower = 2147483647u - toward_upper;
		return multiply_32x32_rshift32(lower, static_cast<int32_t>(toward_lower))
		       + multiply_32x32_rshift32(upper, static_cast<int32_t>(toward_upper));
	}
};

/// The shared stateless instance.
inline constexpr BilinearTableInterpolator bilinear_table_interpolator{};

/// @brief Curve lookup into the `tables::decay_small_8` decay curve.
/// @param input Fixed-point position with @p input_fractional_bits fractional bits, from 0 up to and including
///        2^input_fractional_bits, which reads the table's last entry.
/// @return The interpolated decay value at Q15 (see @ref LinearTableInterpolator::interpolate).
int32_t getDecay8(uint32_t input, uint8_t input_fractional_bits);

/// @brief Curve lookup into the `tables::decay_small_4` decay curve (a different curvature to @ref getDecay8).
/// @param input Fixed-point position with @p input_fractional_bits fractional bits, from 0 up to and including
///        2^input_fractional_bits, which reads the table's last entry.
/// @return The interpolated decay value at Q15 (see @ref LinearTableInterpolator::interpolate).
int32_t getDecay4(uint32_t input, uint8_t input_fractional_bits);

} // namespace deluge::dsp::fixed
