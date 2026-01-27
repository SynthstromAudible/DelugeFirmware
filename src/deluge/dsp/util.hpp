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
#include <cmath>

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

inline void foldBufferPolyApproximation(q31_t* startSample, q31_t* endSample, q31_t level) {
	q31_t* currentSample = startSample;
	q31_t fold_level = add_saturate(level, FOLD_MIN);

	do {
		q31_t c = *currentSample;

		q31_t x = lshiftAndSaturateUnknown(multiply_32x32_rshift32(fold_level, c), 8);

		// volume compensation
		*currentSample = polynomialOscillatorApproximation(x) >> 7;

		currentSample += 1;
	} while (currentSample < endSample);
}
/**
 * foldBuffer folds a whole buffer. Works for stereo too
 */
inline void foldBuffer(q31_t* startSample, q31_t* endSample, q31_t foldLevel) {
	q31_t* currentSample = startSample;
	do {
		q31_t outs = fold(*currentSample, foldLevel);
		// volume compensation
		*currentSample = outs + 4 * multiply_32x32_rshift32(outs, foldLevel);

		currentSample += 1;
	} while (currentSample < endSample);
}
/// Simple unipolar triangle - 2 segments: 0→1→0 (peak at phase=0.5)
/// @param phase Phase in cycles (wraps automatically via floor)
/// @param duty Active portion 0.0-1.0 (default 1.0 = full triangle, no deadzone)
/// @return Output 0.0 to 1.0
inline float triangleSimpleUnipolar(float phase, float duty = 1.0f) {
	// Fast floor via int32_t truncation (valid for non-negative phase)
	phase = phase - static_cast<float>(static_cast<int32_t>(phase));
	float halfDuty = duty * 0.5f;
	float invHalfDuty = 2.0f / duty; // One division instead of two

	if (phase < halfDuty) {
		return phase * invHalfDuty; // Rising: 0→1
	}
	else if (phase < duty) {
		return (duty - phase) * invHalfDuty; // Falling: 1→0
	}
	return 0.0f; // Deadzone
}

/// Bipolar triangle - 4 segments: 0→+1→0→-1→0 (starts at 0, peak at phase=0.25)
/// @param phase Phase in cycles (wraps automatically via floor)
/// @param duty Active portion 0.0-1.0 (default 1.0 = full triangle, no deadzone)
/// @return Output -1.0 to +1.0
inline float triangleFloat(float phase, float duty = 1.0f) {
	// Fast floor via int32_t truncation (valid for non-negative phase)
	phase = phase - static_cast<float>(static_cast<int32_t>(phase));
	float quarterDuty = duty * 0.25f;
	float halfDuty = duty * 0.5f;

	if (phase < quarterDuty) {
		return phase / quarterDuty; // Rising positive: 0→+1
	}
	else if (phase < halfDuty) {
		return (halfDuty - phase) / quarterDuty; // Falling positive: +1→0
	}
	else if (phase < halfDuty + quarterDuty) {
		return -(phase - halfDuty) / quarterDuty; // Falling negative: 0→-1
	}
	else if (phase < duty) {
		return -(duty - phase) / quarterDuty; // Rising negative: -1→0
	}
	return 0.0f; // Deadzone
}

} // namespace deluge::dsp
