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

#include "dsp-next/core/sample.h"

// q31 <-> f32 edge conversions. The core stays q31 (decision 1); float-world
// engines and the CLAP boundary convert here. Scalar + constexpr for now;
// vectorised span overloads land when a float-edge primitive needs them (the
// hardware does this in one NEON op per vector).
namespace deluge::dsp {

// q31 (ONE_Q31 == +1.0) -> f32 in [-1, 1].
[[nodiscard]] constexpr f32 toF32(q31 x) noexcept {
	return static_cast<f32>(x) * (1.0f / ONE_Q31f);
}

// f32 -> q31, saturating to [-1, 1] and rounding to nearest.
//
// Saturation is handled by explicit branches rather than scale-then-clamp: note
// that ONE_Q31f (2^31 - 1) rounds *up* to 2^31 as a float, so x * ONE_Q31f at
// |x| == 1 would land on 2^31 and overflow the int32 cast (UB). The early
// returns keep the cast argument strictly inside the int32 range.
[[nodiscard]] constexpr q31 toQ31(f32 x) noexcept {
	if (x >= 1.0f) {
		return ONE_Q31;
	}
	if (x <= -1.0f) {
		return -ONE_Q31;
	}
	return static_cast<q31>((x * ONE_Q31f) + (x >= 0.0f ? 0.5f : -0.5f));
}

// Compile-time self-tests for the edges that matter (saturation + zero).
static_assert(toQ31(1.0f) == ONE_Q31);
static_assert(toQ31(-1.0f) == -ONE_Q31);
static_assert(toQ31(2.0f) == ONE_Q31);   // saturates
static_assert(toQ31(-2.0f) == -ONE_Q31); // saturates
static_assert(toQ31(0.0f) == 0);
static_assert(toF32(0) == 0.0f);
static_assert(toF32(ONE_Q31) > 0.999f && toF32(ONE_Q31) <= 1.0f);
static_assert(toF32(-ONE_Q31) < -0.999f && toF32(-ONE_Q31) >= -1.0f);

} // namespace deluge::dsp
