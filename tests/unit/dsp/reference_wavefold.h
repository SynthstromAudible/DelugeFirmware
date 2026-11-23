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

// The app's wavefolder before dsp-next owned it, kept as the oracle the Wavefolder spec checks against. It is verbatim
// but for its doublings and ×8, written as the wrapping operations the target computes, so the spec's signed-overflow
// sanitizer sees the same two's-complement result.

#include "raw_words.h"

#include <cstdint>

namespace reference_wavefold {

/// x times n, wrapping modulo 2^32.
inline int32_t wrap_mul(int32_t n, int32_t x) {
	return static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(n));
}

constexpr int32_t FOLD_MIN = 0.1 * kOneQ31;
constexpr int32_t THREE_FOURTHS = 0.75 * kOneQ31;

/**
 * This approximates wavefolding by taking an input between -1 and 1
 * and producing output that flips around zero several times
 */
inline int32_t polynomialOscillatorApproximation(int32_t x) {
	// requires 1 to be kOneQ31

	int32_t x2 = wrap_mul(2, multiply_32x32_rshift32(x, x));
	int32_t x3 = wrap_mul(2, multiply_32x32_rshift32(x2, x));
	// this is 4(3*x/4 - x^3) which is a nice shape
	int32_t r1 = wrap_mul(8, static_cast<int32_t>(static_cast<uint32_t>(multiply_32x32_rshift32(THREE_FOURTHS, x))
	                                              - static_cast<uint32_t>(x3)));

	int32_t r2 = wrap_mul(2, multiply_32x32_rshift32(r1, r1));
	int32_t r3 = wrap_mul(2, multiply_32x32_rshift32(r2, r1));
	// at this point we've applied the polynomial twice
	int32_t out = wrap_mul(8, static_cast<int32_t>(static_cast<uint32_t>(multiply_32x32_rshift32(THREE_FOURTHS, r1))
	                                               - static_cast<uint32_t>(r3)));

	return out;
}

inline void foldBufferPolyApproximation(int32_t* startSample, int32_t* endSample, int32_t level) {
	int32_t* currentSample = startSample;
	int32_t fold_level = add_saturate(level, FOLD_MIN);

	do {
		int32_t c = *currentSample;

		int32_t x = lshift_and_saturate_unknown(multiply_32x32_rshift32(fold_level, c), 8);

		// volume compensation
		*currentSample = polynomialOscillatorApproximation(x) >> 7;

		currentSample += 1;
	} while (currentSample < endSample);
}

} // namespace reference_wavefold
