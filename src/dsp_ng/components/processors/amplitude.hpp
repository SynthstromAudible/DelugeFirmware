/**
 * Copyright (c) 2025 Katherine Whitlock
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
#include "deluge/util/fixedpoint.h"
#include "dsp_ng/core/mixer.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/types.hpp"

namespace deluge::dsp::processors {
/// @brief A processor that applies a stepped gain to an input sample.
/// @deprecated
class AmplitudeStepProcessor final : public Processor<Argon<q31_t>> {
	Argon<int32_t> amplitude_;           ///< The current amplitude value (in q30 format).
	Argon<int32_t> amplitude_increment_; ///< The increment value for the amplitude (in q30 format).
public:
	/// @brief Constructor for the Amplitude class.
	/// @param amplitude The initial amplitude value
	/// @param amplitude_increment The increment value for the amplitude (q30)
	AmplitudeStepProcessor(FixedPoint<31> amplitude, FixedPoint<31> amplitude_increment)
	    : amplitude_increment_{amplitude_increment.raw() * 4},
	      amplitude_{Argon{amplitude.raw()}.MultiplyAdd(int32x4_t{1, 2, 3, 4}, amplitude_increment.raw())} {}

	AmplitudeStepProcessor(FixedPoint<30> amplitude, FixedPoint<30> amplitude_increment)
	    : AmplitudeStepProcessor{static_cast<FixedPoint<31>>(amplitude),
	                             static_cast<FixedPoint<31>>(amplitude_increment)} {}

	/// @brief Process a pair of input samples and return the mixed output sample.
	/// @param input_a The input sample to apply the amplitude to.
	/// @param input_b The existing sample to mix with.
	/// @return The mixed output sample.
	[[gnu::always_inline]] Argon<q31_t> render(Argon<q31_t> input) final {
		Argon<q31_t> output = input.MultiplyFixedPoint(amplitude_);
		amplitude_ = amplitude_ + amplitude_increment_;
		return output;
	}
};
} // namespace deluge::dsp::processors
