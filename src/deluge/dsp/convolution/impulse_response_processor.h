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

#include "dsp/stereo_sample.h"
#include "util/fixedpoint.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

class [[gnu::hot]] ImpulseResponseProcessor {
	constexpr static size_t IR_SIZE = 26;
	constexpr static size_t HISTORY_SIZE = (IR_SIZE - 1);

	constexpr static std::array<int32_t, IR_SIZE> ir = {
	    -3203916,   8857848,   24813136,  41537808, 35217472,  15195632,  -27538592, -61984128, 1944654848,
	    1813580928, 438462784, 101125088, 6042048,  -22429488, -46218864, -56638560, -64785312, -52108528,
	    -37256992,  -11863856, 1390352,   14663296, 12784464,  14254800,  5690912,   4490736,
	};

public:
	ImpulseResponseProcessor() = default;

	/// Convolve the buffer with the IR, in place. Direct form, so each output is a single smmlar chain over the
	/// last IR_SIZE inputs; this gives exactly the same result as accumulating rounded products one at a time.
	void process(std::span<StereoSample> buffer) {
		// Render windows are often shorter than the IR, so rather than special-casing the outputs that reach back
		// into the last call's inputs, lay the history and a chunk of input out contiguously and convolve that
		std::array<StereoSample, HISTORY_SIZE + CHUNK_SIZE> input;
		std::copy(history_.begin(), history_.end(), input.begin());

		while (!buffer.empty()) {
			size_t chunkSize = std::min(buffer.size(), CHUNK_SIZE);
			std::copy_n(buffer.begin(), chunkSize, input.begin() + HISTORY_SIZE);
			convolve(input.data() + HISTORY_SIZE, buffer.first(chunkSize));
			// The end of this chunk's input is the history for the next
			std::copy_n(input.begin() + chunkSize, HISTORY_SIZE, input.begin());
			buffer = buffer.subspan(chunkSize);
		}

		std::copy_n(input.begin(), HISTORY_SIZE, history_.begin());
	}

private:
	constexpr static size_t CHUNK_SIZE = 64;

	/// x points at the first new input, with HISTORY_SIZE older inputs before it
	static void convolve(const StereoSample* x, std::span<StereoSample> output) {
		size_t n = 0;
		// Two outputs at a time: each input loaded is used by both, against neighbouring coefficients
		for (; n + 1 < output.size(); n += 2) {
			const StereoSample* newest = &x[n + 1];
			// Hide the relation to the last pass, or GCC tries to carry the shared inputs over in registers and
			// spills nearly everything
			asm("" : "+r"(newest));
			int32_t l0 = 0; // Output n + 1
			int32_t r0 = 0;
			int32_t l1 = 0; // Output n
			int32_t r1 = 0;
			int32_t previousCoefficient = ir[0];
			StereoSample sample = newest[0];
			l0 = multiply_accumulate_32x32_rshift32_rounded(l0, sample.l, previousCoefficient);
			r0 = multiply_accumulate_32x32_rshift32_rounded(r0, sample.r, previousCoefficient);
#pragma GCC unroll 26
			for (int32_t k = 1; k < IR_SIZE; k++) {
				int32_t coefficient = ir[k];
				sample = newest[-k];
				l0 = multiply_accumulate_32x32_rshift32_rounded(l0, sample.l, coefficient);
				r0 = multiply_accumulate_32x32_rshift32_rounded(r0, sample.r, coefficient);
				l1 = multiply_accumulate_32x32_rshift32_rounded(l1, sample.l, previousCoefficient);
				r1 = multiply_accumulate_32x32_rshift32_rounded(r1, sample.r, previousCoefficient);
				previousCoefficient = coefficient;
			}
			sample = newest[-static_cast<int32_t>(IR_SIZE)];
			l1 = multiply_accumulate_32x32_rshift32_rounded(l1, sample.l, previousCoefficient);
			r1 = multiply_accumulate_32x32_rshift32_rounded(r1, sample.r, previousCoefficient);
			output[n] = {l1, r1};
			output[n + 1] = {l0, r0};
		}

		if (n < output.size()) {
			const StereoSample* newest = &x[n];
			int32_t l = 0;
			int32_t r = 0;
			for (int32_t k = 0; k < IR_SIZE; k++) {
				l = multiply_accumulate_32x32_rshift32_rounded(l, newest[-k].l, ir[k]);
				r = multiply_accumulate_32x32_rshift32_rounded(r, newest[-k].r, ir[k]);
			}
			output[n] = {l, r};
		}
	}

	// The last HISTORY_SIZE inputs, oldest first
	std::array<StereoSample, HISTORY_SIZE> history_{};
};
