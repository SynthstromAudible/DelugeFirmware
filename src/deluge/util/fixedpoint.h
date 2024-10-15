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

#include <cstdint>
// signed 31 fractional bits (e.g. one would be 1<<31 but can't be represented)
using q31_t = int32_t;

constexpr q31_t ONE_Q31{2147483647};
constexpr float ONE_Q31f{2147483647.0f};
constexpr q31_t ONE_Q15{65536};
constexpr q31_t NEGATIVE_ONE_Q31{-2147483648};
constexpr q31_t ONE_OVER_SQRT2_Q31{1518500250};

// This converts the range -2^31 to 2^31 to the range 0-2^31
static inline q31_t toPositive(q31_t a) __attribute__((always_inline, unused));
static inline q31_t toPositive(q31_t a) {
	return ((a / 2) + (1073741824));
}

// this is only defined for 32 bit arm
#if defined(__arm__)
// This multiplies two numbers in signed Q31 fixed point as if they were q32, so the return value is half what it should
// be. Use this when several corrective shifts can be accumulated and then combined
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

// This multiplies two numbers in signed Q31 fixed point, returning the result in q31. This is more useful for readable
// multiplies
static inline q31_t q31_mult(q31_t a, q31_t b) __attribute__((always_inline, unused));
static inline q31_t q31_mult(q31_t a, q31_t b) {
	q31_t out;
	asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out * 2;
}

// This multiplies a number in q31 by a number in q32 (e.g. unsigned, 2^32 representing one), returning a scaled value a
static inline q31_t q31tRescale(q31_t a, uint32_t proportion) __attribute__((always_inline, unused));
static inline q31_t q31tRescale(q31_t a, uint32_t proportion) {
	q31_t out;
	asm("smmul %0, %1, %2" : "=r"(out) : "r"(a), "r"(proportion));
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

// computes limit((val >> rshift), 2**bits)
template <uint8_t bits>
static inline int32_t signed_saturate(int32_t val) __attribute__((always_inline, unused));
template <uint8_t bits>
static inline int32_t signed_saturate(int32_t val) {
	int32_t out;
	asm("ssat %0, %1, %2" : "=r"(out) : "I"(bits), "r"(val));
	return out;
}

static inline int32_t add_saturation(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t add_saturation(int32_t a, int32_t b) {
	int32_t out;
	asm("qadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

inline int32_t clz(uint32_t input) {
	int32_t out;
	asm("clz %0, %1" : "=r"(out) : "r"(input));
	return out;
}
#else

static inline q31_t multiply_32x32_rshift32(q31_t a, q31_t b) {
	return (q31_t)(((int64_t)a * (int64_t)b) >> 32);
}

// This multiplies two numbers in signed Q31 fixed point and rounds the result

static inline q31_t multiply_32x32_rshift32_rounded(q31_t a, q31_t b) {
	return (q31_t)(((int64_t)a * (int64_t)b) >> 32);
}

// Multiplies A and B, adds to sum, and returns output

static inline q31_t multiply_accumulate_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return sum + (q31_t)(((int64_t)a * (int64_t)b) >> 32);
}

// Multiplies A and B, subtracts from sum, and returns output

static inline q31_t multiply_subtract_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return sum - (q31_t)(((int64_t)a * (int64_t)b) >> 32);
}

// computes limit((val >> rshift), 2**bits)
template <uint8_t bits>
static inline int32_t signed_saturate(int32_t val) {
	return std::min(val, 1 << bits);
}

static inline int32_t add_saturation(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t add_saturation(int32_t a, int32_t b) {
	return a + b;
}

inline int32_t clz(uint32_t input) {
	return __builtin_clz(input);
}
#endif
