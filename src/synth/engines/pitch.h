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

#include "definitions_cxx.hpp"     // kMaxSampleValue
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32_rounded
#include <cstdint>

namespace deluge::synth {

/// Scales @p phase_increment (UQ0.32) by @p adjustment, a pitch adjust where kMaxSampleValue (2^24) is unity.
/// @return false when the result is too high to render (above 22.05 kHz); @p phase_increment is then not valid.
[[nodiscard]] inline bool adjust_pitch(uint32_t& phase_increment, int32_t adjustment) {
	if (adjustment != kMaxSampleValue) {
		const int32_t output = multiply_32x32_rshift32_rounded(static_cast<int32_t>(phase_increment), adjustment);
		if (output >= (1 << 23)) {
			return false;
		}
		phase_increment = static_cast<uint32_t>(output) << 8;
	}
	return true;
}

} // namespace deluge::synth
