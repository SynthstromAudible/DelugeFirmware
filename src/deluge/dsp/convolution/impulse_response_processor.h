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
#include <argon.hpp>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

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

	/// Convolve the buffer with the IR, in place. Direct form, so each output is a single chain of rounded
	/// multiply-accumulates over the last IR_SIZE inputs; this gives exactly the same result as accumulating
	/// rounded products one at a time.
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
		convolveScalar(x, output, convolveNeon(x, output));
	}
	// Every coefficient is even, so vqrdmulh by half of one - round(2 * x * (h / 2) / 2^32) - is exactly smmulr by
	// it: round(x * h / 2^32). It can't saturate, as no half coefficient is INT32_MIN
	constexpr static size_t NUM_COEFFICIENT_VECTORS = (IR_SIZE + 3) / 4;
	alignas(16) constexpr static std::array<int32_t, NUM_COEFFICIENT_VECTORS * 4> irHalf = [] {
		std::array<int32_t, NUM_COEFFICIENT_VECTORS * 4> half{};
		for (size_t k = 0; k < IR_SIZE; k++) {
			half[k] = ir[k] / 2;
		}
		return half;
	}();
	using CoefficientVectors = std::array<Argon<int32_t>, NUM_COEFFICIENT_VECTORS>;
	static_assert(std::ranges::all_of(ir, [](int32_t h) { return h % 2 == 0; }));

	template <size_t j>
	[[gnu::always_inline]] static void tap(const int32_t* newest, CoefficientVectors& coefficients,
	                                       Argon<int32_t>& later, Argon<int32_t>& earlier) {
		auto pair = Argon<int32_t>::Load(newest - 2 * static_cast<int32_t>(j));
		if constexpr (j < IR_SIZE) {
			later = later + pair.MultiplyRoundFixedQMax(coefficients[j / 4].GetLane(j % 4));
		}
		if constexpr (j >= 2) {
			earlier = earlier + pair.MultiplyRoundFixedQMax(coefficients[(j - 2) / 4].GetLane((j - 2) % 4));
		}
	}

	/// Four outputs (two vectors, each two stereo frames) at a time; returns how many outputs it did
	static size_t convolveNeon(const StereoSample* x, std::span<StereoSample> output) {
		CoefficientVectors coefficients;
		for (size_t i = 0; i < coefficients.size(); i++) {
			coefficients[i] = Argon<int32_t>::Load(&irHalf[4 * i]);
		}

		size_t n = 0;
		for (; n + 3 < output.size(); n += 4) {
			// Outputs n + 2 and n + 3 take input pair j with coefficient j; outputs n and n + 1 take the same pair
			// with coefficient j - 2
			const int32_t* newest = &x[n + 2].l;
			Argon<int32_t> later{0};
			Argon<int32_t> earlier{0};
			[&]<size_t... j>(std::index_sequence<j...>) {
				(tap<j>(newest, coefficients, later, earlier), ...);
			}(std::make_index_sequence<IR_SIZE + 2>{});
			earlier.StoreTo(&output[n].l);
			later.StoreTo(&output[n + 2].l);
		}
		return n;
	}

	/// One output at a time, from output n on: the leftovers after convolveNeon, or everything off ARM
	static void convolveScalar(const StereoSample* x, std::span<StereoSample> output, size_t n = 0) {
		for (; n < output.size(); n++) {
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
