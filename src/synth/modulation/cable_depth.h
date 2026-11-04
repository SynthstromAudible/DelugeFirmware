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

#include "dsp-next/core/fixed_point.h"
#include "modulation/params/param.h" // the pitch-adjust params, GLOBAL_DELAY_RATE
#include "synth/modulation/cable.h"

#include <cstdint>
#include <limits>

/// @file
/// @brief The matrix's per-cable arithmetic: how a cable reads its source, how a depth cable's range value scales a
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
		// the unipolar Q31 centred one LSB under a half and doubled, wrapping
		return scale_shl_wrap<1>(
		           sub_wrap(dsp::Q<31>::from_raw(value), dsp::Q<31>::from_raw(std::numeric_limits<int32_t>::max() / 2)))
		    .raw();
	}
	if (from == PatchSource::Y || polarity == CablePolarity::BIPOLAR) {
		return value;
	}
	// half the bipolar Q31, rounded toward zero, centred one LSB under a half
	return add_wrap(halve_toward_zero(dsp::Q<31>::from_raw(value)),
	                dsp::Q<31>::from_raw(std::numeric_limits<int32_t>::max() / 2))
	    .raw();
}

/// @brief @p value scaled by a depth cable's range value @p range (Q29: 536870912 is 1), both Q29, and held to
/// [−1, 1): the product is Q26, clamped to its 27 bits and moved back to Q29, so a ranged term never passes "1".
[[nodiscard]] [[gnu::always_inline]] inline int32_t apply_range(int32_t value, int32_t range) {
	const dsp::Q<26> product = mul(dsp::Q<29>::from_raw(value), dsp::Q<29>::from_raw(range));
	return as_wrap<dsp::Q<29>>(clamp_bits<27>(product)).raw();
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
		// The Q30 depth squared to Q29, its sign kept: raw, an integer product of the two truncated factors
		int32_t output = (depth >> 15) * (depth >> 16);
		if (depth < 0) {
			output = -output;
		}
		if (p == params::LOCAL_PITCH_ADJUST) { // times 2/3 for velocity, 1/√2 for any other source
			const auto scale = dsp::Q<31>::from_raw(from == PatchSource::VELOCITY ? 1431655765 : 1518500250);
			output = mul_r_as_wrap<dsp::Q<29>>(dsp::Q<29>::from_raw(output), scale).raw();
		}
		return output;
	}
	default:
		return depth;
	}
}

} // namespace deluge::synth
