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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/q31_ops.h" // q31_t, kOneQ31, kOneQ31f (standard-headers-only, L0-clean)
#include <concepts>

// Numeric vocabulary for dsp-next. The library is numeric-agnostic at the
// interface (decision 1): a primitive is written once against a SampleType and
// realised in the q31 (fixed/) and f32 (float/) backends.
namespace deluge::dsp {

// q31 fixed-point sample: Q<31>, where Q<31>::max() (2^31 - 1) represents +1.0.
//
// A typed format, not a raw int32: every operation on a sample names its overflow
// and rounding (fixed_point.h), and the format travels with the value. The app's
// raw q31_t meets it only at the seam (buffer_view.h's typed_view and raw_view,
// from_raw and raw()).
using q31 = Q<31>;

// float32 sample: nominal range [-1, 1].
using f32 = float;

// The scalar types a dsp-next primitive may be instantiated over.
template <typename T>
concept sample_type = std::same_as<T, q31> || std::same_as<T, f32>;

} // namespace deluge::dsp
