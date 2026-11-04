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
[[nodiscard]] [[gnu::always_inline]] inline dsp::Q<31> to_polarity(PatchSource from, CablePolarity polarity,
                                                                   dsp::Q<31> value) {
	constexpr auto kHalf = dsp::Q<31>::from_raw(std::numeric_limits<int32_t>::max() / 2);
	if (from == PatchSource::AFTERTOUCH) {
		if (polarity == CablePolarity::UNIPOLAR) {
			return value;
		}
		// the unipolar value centred one LSB under a half and doubled, wrapping
		return scale_shl_wrap<1>(sub_wrap(value, kHalf));
	}
	if (from == PatchSource::Y || polarity == CablePolarity::BIPOLAR) {
		return value;
	}
	// half the bipolar value, rounded toward zero, centred one LSB under a half
	return add_wrap(halve_toward_zero(value), kHalf);
}

/// @brief @p value scaled by a depth cable's range value @p range (2^29 is 1), and held to [−1, 1): the product is
/// Q26, clamped to its 27 bits and moved back to Q29, so a ranged term never passes "1".
[[nodiscard]] [[gnu::always_inline]] inline dsp::Q<29> apply_range(dsp::Q<29> value, dsp::Q<29> range) {
	return as_wrap<dsp::Q<29>>(clamp_bits<27>(mul(value, range)));
}

/// @brief The depth a cable from @p from to param @p p applies: squared for the pitch adjusts and the delay rate, so
/// small depths stay small, and for the master pitch scaled so velocity's steps are semitones (and other sources get
/// three octaves rather than four).
/// @param depth The cable's depth, [-1, 1].
[[nodiscard]] inline dsp::Q<30> modified_depth(dsp::Q<30> depth, PatchSource from, int32_t p) {
	namespace params = modulation::params;
	switch (p) {
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_RATE: {
		// The depth squared to Q29, its sign kept: raw, an integer product of the two truncated factors
		int32_t output = (depth.raw() >> 15) * (depth.raw() >> 16);
		if (depth.raw() < 0) {
			output = -output;
		}
		auto squared = dsp::Q<29>::from_raw(output);
		if (p == params::LOCAL_PITCH_ADJUST) { // times 2/3 for velocity, 1/√2 for any other source
			const auto scale = dsp::Q<31>::from_raw(from == PatchSource::VELOCITY ? 1431655765 : 1518500250);
			squared = mul_r_as_wrap<dsp::Q<29>>(squared, scale);
		}
		// The square's word is the shaped depth: a full depth's square is 2^29, half the plain one's reach
		return dsp::reinterpret_bits<dsp::Q<30>>(squared);
	}
	default:
		return depth;
	}
}

} // namespace deluge::synth
