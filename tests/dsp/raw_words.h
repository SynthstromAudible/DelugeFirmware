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

/// @file
/// @brief The raw references' vocabulary: the raw word helpers, by their own names, and the full-scale words the
/// references were written with. The typed operations are checked against these; new specs use fixed_point.h's
/// operations.

#pragma once

#include "dsp-next/core/detail/raw_ops.h"

#include <cstdint>

using deluge::dsp::detail::add_saturate;
using deluge::dsp::detail::clz;
using deluge::dsp::detail::increase_magnitude_and_saturate;
using deluge::dsp::detail::lshift_and_saturate;
using deluge::dsp::detail::lshift_and_saturate_unknown;
using deluge::dsp::detail::multiply_32x32_rshift32;
using deluge::dsp::detail::multiply_32x32_rshift32_rounded;
using deluge::dsp::detail::multiply_accumulate_32x32_rshift32;
using deluge::dsp::detail::multiply_accumulate_32x32_rshift32_rounded;
using deluge::dsp::detail::multiply_subtract_32x32_rshift32_rounded;
using deluge::dsp::detail::q31_from_float;
using deluge::dsp::detail::q31_mult;
using deluge::dsp::detail::q31_to_float;
using deluge::dsp::detail::q31t_rescale;
using deluge::dsp::detail::signed_saturate;
using deluge::dsp::detail::signed_saturate_operand_unknown;
using deluge::dsp::detail::subtract_saturate;

inline constexpr int32_t kOneQ31 = INT32_MAX;           ///< Full scale, 2^31 − 1.
inline constexpr float kOneQ31f = 2147483647.0F;        ///< Full scale as a float: 2^31, the float nearest.
inline constexpr int32_t kOneQ15 = 65536;               ///< 1.0 in the references' 16.16 words.
inline constexpr int32_t kOneOverSqrt2Q31 = 1518500250; ///< 1/√2, Q31.

/// @brief A Q31 word mapped from [−1, 1) onto [0, 1): half of it, plus a half.
inline int32_t to_positive(int32_t a) {
	return (a / 2) + 1073741824;
}
