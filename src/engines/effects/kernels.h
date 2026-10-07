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

#include "util/fixedpoint.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::effects {

/// @brief The rows of the antialiased tanh table: one per 2^26 step of the previous working value, plus one.
inline constexpr size_t kTanH2dRows = 65;
/// @brief The entries of the exp curve's table: one octave in 256 steps, plus one.
inline constexpr size_t kExpTableSmallSize = 257;

/// @brief The antialiased tanh's table: rows by the previous working value, 129 entries by the current (Q15).
extern const std::array<std::array<int16_t, 129>, kTanH2dRows> kTanH2d;
/// @brief One octave of the exp curve, 2^15 to 2^16 (unsigned Q15).
extern const std::array<uint16_t, kExpTableSmallSize> kExpTableSmall;

namespace detail {

/// @p val saturated to @p bits signed bits (12 to 31); a switch over the saturating instruction's fixed widths.
[[nodiscard, gnu::always_inline]] inline int32_t saturate_to_bits(int32_t val, int32_t bits) {
	switch (bits) {
	case 31:
		return signed_saturate<31>(val);
	case 30:
		return signed_saturate<30>(val);
	case 29:
		return signed_saturate<29>(val);
	case 28:
		return signed_saturate<28>(val);
	case 27:
		return signed_saturate<27>(val);
	case 26:
		return signed_saturate<26>(val);
	case 25:
		return signed_saturate<25>(val);
	case 24:
		return signed_saturate<24>(val);
	case 23:
		return signed_saturate<23>(val);
	case 22:
		return signed_saturate<22>(val);
	case 21:
		return signed_saturate<21>(val);
	case 20:
		return signed_saturate<20>(val);
	case 19:
		return signed_saturate<19>(val);
	case 18:
		return signed_saturate<18>(val);
	case 17:
		return signed_saturate<17>(val);
	case 16:
		return signed_saturate<16>(val);
	case 15:
		return signed_saturate<15>(val);
	case 14:
		return signed_saturate<14>(val);
	case 13:
		return signed_saturate<13>(val);
	default:
		return signed_saturate<12>(val);
	}
}

/// @p val shifted left by @p lshift (more than 0), saturating rather than wrapping.
[[nodiscard, gnu::always_inline]] inline int32_t lshift_and_saturate(int32_t val, uint8_t lshift) {
	return saturate_to_bits(val, 32 - lshift) << lshift;
}

} // namespace detail

/// @brief Signed linear interpolation into @p table (16-bit entries, 2^bits_in_table + 1 of them) by the top bits of
/// @p input, which has @p bits_in_input bits. The result is the entries scaled by 65536.
[[nodiscard, gnu::always_inline]] inline int32_t
interpolate_table_signed(uint32_t input, int32_t bits_in_input, const int16_t* table, int32_t bits_in_table = 8) {
	int32_t whichValue = input >> (bits_in_input - bits_in_table);
	int32_t rshiftAmount = bits_in_input - 16 - bits_in_table;
	uint32_t rshifted;
	if (rshiftAmount >= 0) {
		rshifted = input >> rshiftAmount;
	}
	else {
		rshifted = input << (-rshiftAmount);
	}
	int32_t strength2 = rshifted & 65535;
	int32_t strength1 = 65536 - strength2;
	return static_cast<int32_t>(table[whichValue]) * strength1
	       + static_cast<int32_t>(table[whichValue + 1]) * strength2;
}

/// @brief Bilinear interpolation into a 2-D table of rows of 2^bits_in_table_x + 1 entries; the output is
/// ±1073741824.
[[nodiscard, gnu::always_inline]] inline int32_t
interpolate_table_signed_2d(uint32_t input_x, uint32_t input_y, int32_t bits_in_x, int32_t bits_in_y,
                            const int16_t* table, int32_t bits_in_table_x, int32_t bits_in_table_y) {
	const uint32_t inputX = input_x;
	const uint32_t inputY = input_y;
	const int32_t numBitsInInputX = bits_in_x;
	const int32_t numBitsInInputY = bits_in_y;
	const int32_t numBitsInTableSizeX = bits_in_table_x;
	const int32_t numBitsInTableSizeY = bits_in_table_y;

	int32_t whichValue = inputY >> (numBitsInInputY - numBitsInTableSizeY);

	int32_t tableSizeOneRow = (1 << numBitsInTableSizeX) + 1;

	int32_t value1 =
	    interpolate_table_signed(inputX, numBitsInInputX, &table[whichValue * tableSizeOneRow], numBitsInTableSizeX);
	int32_t value2 = interpolate_table_signed(inputX, numBitsInInputX, &table[(whichValue + 1) * tableSizeOneRow],
	                                          numBitsInTableSizeX);

	int32_t lshiftAmount = 31 + numBitsInTableSizeY - numBitsInInputY;

	uint32_t strength2;

	if (lshiftAmount >= 0)
		strength2 = (inputY << lshiftAmount) & 2147483647;
	else
		strength2 = (inputY >> (0 - lshiftAmount)) & 2147483647;

	uint32_t strength1 = 2147483647 - strength2;
	return multiply_32x32_rshift32(value1, strength1) + multiply_32x32_rshift32(value2, strength2);
}

/// @brief Antialiased tanh of the Q31 @p input saturated by 2^amount; @p last is the previous call's working value
/// for this channel (initially 2^31) and is updated. Inline: a voice calls it for every sample.
[[nodiscard, gnu::always_inline]] inline int32_t tanh_antialiased(int32_t input, uint32_t& last, uint32_t amount) {
	const uint32_t working =
	    static_cast<uint32_t>(detail::lshift_and_saturate(input, static_cast<uint8_t>(amount))) + 2147483648u;
	const int32_t result = interpolate_table_signed_2d(working, last, 32, 32, kTanH2d[0].data(), 7, 6) >> (amount + 1);
	last = working;
	return result;
}

/// @brief @p preset scaled by 2^(adjustment / 2^26 + 2), the exp curve every param uses (adjustment's low 26 bits
/// are the fraction of an octave).
[[nodiscard]] int32_t exp_of(int32_t preset, int32_t adjustment);

/// @brief The left and right amplitudes of a pan, Q30 (1073741823 is unity).
struct PanAmplitudes {
	int32_t left;
	int32_t right;
};

/// @brief The amplitudes of a pan of @p pan (Q31, 0 is the centre; ±2^30 is fully right or left).
[[nodiscard]] PanAmplitudes pan_amplitudes(int32_t pan);

} // namespace deluge::effects
