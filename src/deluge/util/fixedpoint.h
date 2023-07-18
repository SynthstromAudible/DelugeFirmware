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
#include "math.h"

//signed 31 fractional bits (e.g. one would be 1<<31 but can't be represented)
typedef int32_t q31_t;

#define ONE_Q31 2147483647

// This multiplies two numbers in signed Q31 fixed point and truncates the result
static inline q31_t multiply_32x32_rshift32(q31_t a, q31_t b) __attribute__((always_inline, unused));
static inline q31_t multiply_32x32_rshift32(q31_t a, q31_t b) {
	q31_t out;
	asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

// This multiplies two numbers in signed Q31 fixed point and rounds the result
static inline q31_t multiply_32x32_rshift32_rounded(q31_t a, q31_t b) __attribute__((always_inline, unused));
static inline q31_t multiply_32x32_rshift32_rounded(q31_t a, q31_t b) {
	q31_t out;
	asm("smmulr %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

// Multiplies A and B, adds to sum, and returns output
static inline q31_t multiply_accumulate_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b)
    __attribute__((always_inline, unused));
static inline q31_t multiply_accumulate_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	q31_t out;
	asm("smmlar %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
	return out;
}

// Multiplies A and B, subtracts from sum, and returns output
static inline q31_t multiply_subtract_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b)
    __attribute__((always_inline, unused));
static inline q31_t multiply_subtract_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	q31_t out;
	asm("smmlsr %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
	return out;
}
