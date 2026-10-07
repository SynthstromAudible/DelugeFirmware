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

/// @brief Signed linear interpolation into @p table (16-bit entries, 2^bits_in_table + 1 of them) by the top bits of
/// @p input, which has @p bits_in_input bits. The result is the entries scaled by 65536.
[[nodiscard]] int32_t interpolate_table_signed(uint32_t input, int32_t bits_in_input, const int16_t* table,
                                               int32_t bits_in_table = 8);

/// @brief Bilinear interpolation into a 2-D table of rows of 2^bits_in_table_x + 1 entries; the output is
/// ±1073741824.
[[nodiscard]] int32_t interpolate_table_signed_2d(uint32_t input_x, uint32_t input_y, int32_t bits_in_x,
                                                  int32_t bits_in_y, const int16_t* table, int32_t bits_in_table_x,
                                                  int32_t bits_in_table_y);

/// @brief Antialiased tanh of the Q31 @p input saturated by 2^amount; @p last is the previous call's working value
/// for this channel (initially 2^31) and is updated.
[[nodiscard]] int32_t tanh_antialiased(int32_t input, uint32_t& last, uint32_t amount);

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
