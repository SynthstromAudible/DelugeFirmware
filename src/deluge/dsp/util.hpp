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
#include "deluge/util/fixedpoint.h"
#include "deluge/util/functions.h"
#include "dsp/stereo_sample.h"
#include <cmath>
#include <span>

namespace deluge::dsp {
/**
 * Fold reduces the input by the amount it's over the level
 */
constexpr q31_t FOLD_MIN = 0.1 * ONE_Q31;
constexpr q31_t THREE_FOURTHS = 0.75 * ONE_Q31;
inline q31_t fold(q31_t input, q31_t level) {
	// no folding occurs if max is 0 or if max is greater than input
	// to keep the knob range consistent fold starts from 0 and
	// increases, decreasing would lead to a large deadspace until
	// suddenly clipping occured
	// note 9db loss
	q31_t extra = 0;
	q31_t max = level >> 8;
	if (input > max) {
		extra = input - max;
	}
	else if (input < -max) {
		extra = input + max;
	}
	// this avoids inverting the wave
	return 2 * extra - input;
}
/**
 * This approximates wavefolding by taking an input between -1 and 1
 * and producing output that flips around zero several times
 */
inline q31_t polynomialOscillatorApproximation(q31_t x) {
	// requires 1 to be ONE_Q31

	q31_t x2 = 2 * multiply_32x32_rshift32(x, x);
	q31_t x3 = 2 * multiply_32x32_rshift32(x2, x);
	// this is 4(3*x/4 - x^3) which is a nice shape
	q31_t r1 = 8 * (multiply_32x32_rshift32(THREE_FOURTHS, x) - x3);

	q31_t r2 = 2 * multiply_32x32_rshift32(r1, r1);
	q31_t r3 = 2 * multiply_32x32_rshift32(r2, r1);
	// at this point we've applied the polynomial twice
	q31_t out = 8 * (multiply_32x32_rshift32(THREE_FOURTHS, r1) - r3);

	return out;
}

inline void foldBufferPolyApproximation(std::span<q31_t> buffer, q31_t level) {
	q31_t fold_level = add_saturate(level, FOLD_MIN);
	for (auto& sample : buffer) {
		q31_t x = lshiftAndSaturateUnknown(multiply_32x32_rshift32(fold_level, sample), 8);
		// volume compensation
		sample = polynomialOscillatorApproximation(x) >> 7;
	}
}

inline void foldBufferPolyApproximation(std::span<StereoSample> buffer, q31_t level) {
	foldBufferPolyApproximation(std::span<q31_t>{reinterpret_cast<q31_t*>(buffer.data()), buffer.size() * 2}, level);
}

/**
 * foldBuffer folds a whole buffer. Works for stereo too
 */
inline void foldBuffer(std::span<q31_t> buffer, q31_t foldLevel) {
	for (auto& sample : buffer) {
		auto out = fold(sample, foldLevel);
		// volume compensation
		sample = out + 4 * multiply_32x32_rshift32(out, foldLevel);
	}
}
} // namespace deluge::dsp
