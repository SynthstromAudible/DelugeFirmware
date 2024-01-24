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
#include "fixedpoint.h"
#include "util/functions.h"
// This is a very very rough approximation
static inline q31_t crappy_square_root(q31_t input) {
	int32_t leading = clz(input);
	return ONE_Q31 >> (leading / 2);
}
static inline q31_t approximate_inverse_square_root(q31_t raw) {
	q31_t var1, temp, x_squared, in;

	in = raw >> 1; // better range for function
	// 2 rounds of newtons method for isqrt
	// start at one power of two larger than the input
	// for an approximate first guess

	// update is x_nxt = x*(3-input(x^2)/2
	var1 = crappy_square_root(in);

	x_squared = multiply_32x32_rshift32_rounded(var1, var1) << 1;
	temp = multiply_32x32_rshift32_rounded(in, x_squared) << 1;
	temp = 0x30000000 - temp;
	var1 = multiply_32x32_rshift32_rounded(var1, temp) << 2;

	x_squared = multiply_32x32_rshift32_rounded(var1, var1) << 1;
	temp = multiply_32x32_rshift32_rounded(in, x_squared) << 1;
	temp = 0x30000000 - temp;
	var1 = multiply_32x32_rshift32_rounded(var1, temp) << 2;

	x_squared = multiply_32x32_rshift32_rounded(var1, var1) << 1;
	temp = multiply_32x32_rshift32_rounded(in, x_squared) << 1;
	temp = 0x30000000 - temp;
	var1 = multiply_32x32_rshift32_rounded(var1, temp) << 2;

	return (var1 << 2);
}

// This is off by almost a factor of two for in close to 1 but somewhat accurate for
// small inputs
static inline q31_t approximate_square_root(q31_t in) {
	q31_t isqrt = approximate_inverse_square_root(in);
	return multiply_32x32_rshift32_rounded(in, isqrt) << 1;
}
