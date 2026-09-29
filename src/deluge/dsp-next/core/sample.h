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

#include "util/fixedpoint.h" // q31_t, ONE_Q31, ONE_Q31f (standard-headers-only, L0-clean)
#include <concepts>

// Numeric vocabulary for dsp-next. The library is numeric-agnostic at the
// interface (decision 1): a primitive is written once against a SampleType and
// realised in the q31 (fixed/) and f32 (float/) backends.
namespace deluge::dsp {

// q31 fixed-point sample: int32 where ONE_Q31 (2^31 - 1) represents +1.0.
//
// Deliberately an *alias* of the firmware-wide q31_t, not a strong wrapper type.
// During the strangler migration every app boundary speaks q31_t; an opaque
// wrapper would force a conversion at each of those boundaries and buy nothing —
// numeric-agnosticism here comes from templating primitives over SampleType and
// providing two backends, not from wrapping the scalar. (A wrapper could be
// revisited once dsp-next is the only DSP library.)
using q31 = q31_t;

// float32 sample: nominal range [-1, 1].
using f32 = float;

// The scalar types a dsp-next primitive may be instantiated over.
template <typename T>
concept SampleType = std::same_as<T, q31> || std::same_as<T, f32>;

} // namespace deluge::dsp
