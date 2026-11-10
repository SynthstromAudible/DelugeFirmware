// Comb filter class declaration
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dsp/reverb/freeverb/delay_lines.hpp"
#include "dsp/reverb/freeverb/tuning.h"
#include "util/fixedpoint.h"
#include <cstdint>
#include <limits>
#include <span>

namespace freeverb {
// All the comb filters: numcombs for the left channel, then numcombs for the right. They all have the same damping and
// feedback.
class Combs {
public:
	Combs() = default;

	// For comb i; the last maxblockframes words of the buffer are spare
	void setBuffer(size_t i, std::span<int32_t> buffer) { lines_.setBuffer(i, buffer); }

	void mute() { lines_.mute(); }

	constexpr void setDamp(float val) {
		damp1_ = val * std::numeric_limits<int32_t>::max();
		damp2_ = std::numeric_limits<int32_t>::max() - damp1_;
	}

	constexpr void setFeedback(int32_t val) { feedback_ = val; }

	// One frame: adds the left combs' outputs to out_l and the right ones' to out_r
	[[gnu::always_inline]] void process(int32_t input, int32_t& out_l, int32_t& out_r) {
		for (size_t i = 0; i < kNumCombs; i++) {
			int32_t output = lines_[i];

			filterstore_[i] = (multiply_32x32_rshift32_rounded(output, damp2_) + //<
			                   multiply_32x32_rshift32_rounded(filterstore_[i], damp1_))
			                  << 1;

			lines_[i] = input + (multiply_32x32_rshift32_rounded(filterstore_[i], feedback_) << 1);

			((i < numcombs) ? out_l : out_r) += output;
		}
		lines_.advance();
	}

	// Exactly the same as process() for each frame of a block (a multiple of four frames long, at most
	// maxblockframes), but storing the sums in output_l and output_r
	void processBlock(const int32_t* input, int32_t* output_l, int32_t* output_r, size_t numFrames);

private:
	static constexpr size_t kNumCombs = 2 * numcombs;

	DelayLines<kNumCombs> lines_;
	alignas(16) std::array<int32_t, kNumCombs> filterstore_{};
	int32_t damp1_;
	int32_t damp2_;
	int32_t feedback_;
};
} // namespace freeverb
// ends
