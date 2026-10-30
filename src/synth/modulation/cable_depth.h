/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "dsp-next/core/q31_ops.h"   // multiply_32x32_rshift32(_rounded), signed_saturate
#include "modulation/params/param.h" // the pitch-adjust params, GLOBAL_DELAY_RATE
#include "synth/modulation/cable.h"

#include <cstdint>
#include <limits>

/// @file
/// @brief The Patcher's per-cable arithmetic: how a cable reads its source, how a depth cable's range value scales a
/// cable's depth, and the depth curve some params apply.

namespace deluge::synth {

/// @brief A source value as a cable of @p polarity from @p from reads it. Aftertouch is stored unipolar, every other
/// source bipolar; Y carries both the unipolar mod wheel and bipolar MPE Y, so it is never converted.
[[nodiscard]] [[gnu::always_inline]] inline int32_t to_polarity(PatchSource from, CablePolarity polarity,
                                                                int32_t value) {
	if (from == PatchSource::AFTERTOUCH) {
		if (polarity == CablePolarity::UNIPOLAR) {
			return value;
		}
		return (value - (std::numeric_limits<int32_t>::max() / 2)) * 2;
	}
	if (from == PatchSource::Y || polarity == CablePolarity::BIPOLAR) {
		return value;
	}
	return (value / 2) + (std::numeric_limits<int32_t>::max() / 2);
}

/// @brief @p value scaled by a depth cable's range value @p range (Q29: 536870912 is 1).
[[nodiscard]] [[gnu::always_inline]] inline int32_t apply_range(int32_t value, int32_t range) {
	const int32_t small = multiply_32x32_rshift32(value, range);
	return signed_saturate<32 - 5>(small) << 3;
}

/// @brief The depth a cable from @p from to param @p p applies: squared for the pitch adjusts and the delay rate, so
/// small depths stay small, and for the master pitch scaled so velocity's steps are semitones (and other sources get
/// three octaves rather than four).
/// @param depth The cable's depth, [-2^30, 2^30].
[[nodiscard]] inline int32_t modified_depth(int32_t depth, PatchSource from, int32_t p) {
	namespace params = modulation::params;
	switch (p) {
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_RATE: {
		int32_t output = (depth >> 15) * (depth >> 16);
		if (depth < 0) {
			output = -output;
		}
		if (p == params::LOCAL_PITCH_ADJUST) {
			output = from == PatchSource::VELOCITY ? multiply_32x32_rshift32_rounded(output, 1431655765) << 1
			                                       : multiply_32x32_rshift32_rounded(output, 1518500250) << 1;
		}
		return output;
	}
	default:
		return depth;
	}
}

} // namespace deluge::synth
