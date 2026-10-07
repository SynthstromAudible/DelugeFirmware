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

#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A one-pole low-pass section with its memory (Q31): the ladders' (and the compressor's and the granular
/// processor's) building block. Each step moves the memory toward the input by the moveability.
///
/// Every method takes the moveability tan(f)/(1+tan(f)) at Q31, where f is the cutoff as a fraction of the sample
/// rate times π.
class OnePole {
public:
	/// @brief One sample through the low-pass (doFilter): returns the section's output, Q31, and advances its memory.
	/// @param moveability tan(f)/(1+tan(f)), Q31.
	[[nodiscard, gnu::always_inline]] q31 lowPass(q31 input, q31 moveability) {
		const q31 step = multiply_32x32_rshift32_rounded(input - memory_, moveability) << 1;
		const q31 output = step + memory_;
		memory_ = output + step;
		return output;
	}

	/// @brief One sample through the all-pass built on the same section (doAPF): twice the low-pass output less the
	/// input, Q31.
	/// @param moveability tan(f)/(1+tan(f)), Q31.
	[[nodiscard, gnu::always_inline]] q31 allPass(q31 input, q31 moveability) {
		const q31 step = multiply_32x32_rshift32_rounded(input - memory_, moveability) << 1;
		const q31 output = step + memory_;
		memory_ = step + output;
		return (output * 2) - input;
	}

	/// @brief Advances the memory by one sample without an output (affectFilter): the memory moves by twice the
	/// low-pass's step.
	/// @param moveability tan(f)/(1+tan(f)), Q31.
	[[gnu::always_inline]] void affect(q31 input, q31 moveability) {
		memory_ += multiply_32x32_rshift32_rounded(input - memory_, moveability) << 2;
	}

	/// @brief The memory scaled for a ladder's feedback (getFeedbackOutput): memory × @p feedback_amount (Q31) × 2.
	[[nodiscard, gnu::always_inline]] q31 feedbackOutput(q31 feedback_amount) const {
		return multiply_32x32_rshift32_rounded(memory_, feedback_amount) << 2;
	}

	/// @brief The memory scaled for a ladder's feedback, at half scale (getFeedbackOutputWithoutLshift): memory ×
	/// @p feedback_amount (Q31) ÷ 2, for callers that sum several before shifting.
	[[nodiscard, gnu::always_inline]] q31 feedbackOutputUnshifted(q31 feedback_amount) const {
		return multiply_32x32_rshift32_rounded(memory_, feedback_amount);
	}

	/// @brief Silences the memory.
	void reset() { memory_ = 0; }

	/// @brief The memory, Q31.
	[[nodiscard]] q31 memory() const { return memory_; }

private:
	q31 memory_{0};
};

} // namespace deluge::dsp::fixed
