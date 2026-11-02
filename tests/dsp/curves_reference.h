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
// Reference copies of dsp-next's curve functions (instant_tan and exp_curve) as they were in raw q31, kept
// verbatim as the oracle for their typed forms in dsp-next/fixed/curves.
#pragma once

#include "dsp-next/core/q31_ops.h"
#include "dsp-next/fixed/curves/exp_curve.h"
#include "dsp-next/fixed/curves/instant_tan.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace curves_reference {

using deluge::dsp::fixed::kExpSaturatingOctaves;
using deluge::dsp::fixed::kExpTable;
using deluge::dsp::fixed::kLinearTableInterpolator;
using deluge::dsp::fixed::kTanTable;

inline int32_t instant_tan(int32_t input) {
	const int32_t which = input >> 25;
	const int32_t toward_next = (input << 6) & 2147483647;
	const int32_t lower = kTanTable[which];
	const int32_t upper = kTanTable[which + 1];
	return (multiply_32x32_rshift32(upper, toward_next) + multiply_32x32_rshift32(lower, 2147483647 - toward_next))
	       << 1;
}

inline int32_t exp_curve(int32_t preset, int32_t adjustment) {
	const int32_t octaves = (adjustment >> 26) + 2;
	const int32_t within_octave = multiply_32x32_rshift32(
	    preset, kLinearTableInterpolator.interpolate(adjustment & 67108863, 26, kExpTable.data()));
	if (octaves <= kExpSaturatingOctaves) {
		return increase_magnitude_and_saturate(within_octave, octaves);
	}
	// 2^31 times 2^32 still fits in 64 bits; a nonzero value saturates long before
	const int64_t scaled = static_cast<int64_t>(within_octave) << std::min(octaves, 32);
	return static_cast<int32_t>(
	    std::clamp<int64_t>(scaled, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

} // namespace curves_reference
