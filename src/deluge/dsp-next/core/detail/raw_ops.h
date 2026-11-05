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

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "dsp-next/core/high_word_asm.h"

/// @file
/// @brief The raw word arithmetic the typed operations are checked against, and the few raw sites that say why they
/// are raw: the A9's high-word multiplies, multiply-accumulates and saturating ops (one instruction each on ARM, with
/// exact portable twins elsewhere), and the shift-and-saturate helpers built on them. Not for new code: use
/// fixed_point.h's operations.

namespace deluge::dsp::detail {

// this is only defined for 32 bit arm
#if defined(__arm__)
/// This multiplies two numbers in signed Q31 fixed point as if they were q32, so the return value is half what it
/// should be. Use this when several corrective shifts can be accumulated and then combined
[[gnu::always_inline]] inline int32_t multiply_32x32_rshift32(int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmul(a, b);
}

/// This multiplies two numbers in signed Q31 fixed point and rounds the result
[[gnu::always_inline]] inline int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmulr(a, b);
}

/// This multiplies two numbers in signed Q31 fixed point, returning the result in q31. This is more useful for readable
/// multiplies
[[gnu::always_inline]] inline int32_t q31_mult(int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmul(a, b) * 2;
}

/// This multiplies a number in q31 by a number in q32 (e.g. unsigned, 2^32 representing one), returning a scaled value
/// a
[[gnu::always_inline]] inline int32_t q31t_rescale(int32_t a, uint32_t proportion) {
	// The same bits as the uint32: smmul reads them as signed
	return deluge::dsp::detail::arm::smmul(a, static_cast<int32_t>(proportion));
}

/// Multiplies A and B, adds to sum, and returns output
[[gnu::always_inline]] inline int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmlar(sum, a, b);
}

/// Multiplies A and B, adds to sum, and returns output
[[gnu::always_inline]] inline int32_t multiply_accumulate_32x32_rshift32(int32_t sum, int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmla(sum, a, b);
}

/// Multiplies A and B, subtracts from sum, and returns output
[[gnu::always_inline]] inline int32_t multiply_subtract_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b) {
	return deluge::dsp::detail::arm::smmlsr(sum, a, b);
}

/// computes limit((val >> rshift), 2**bits)
template <uint8_t bits>
[[gnu::always_inline]] inline int32_t signed_saturate(int32_t val) {
	int32_t out;
	asm("ssat %0, %1, %2" : "=r"(out) : "I"(bits), "r"(val));
	return out;
}

[[gnu::always_inline]] inline int32_t add_saturate(int32_t a, int32_t b) {
	int32_t out;
	asm("qadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

[[gnu::always_inline]] inline int32_t subtract_saturate(int32_t a, int32_t b) {
	int32_t out;
	asm("qsub %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

[[gnu::always_inline]] inline int32_t clz(uint32_t input) {
	int32_t out;
	asm("clz %0, %1" : "=r"(out) : "r"(input));
	return out;
}

///@brief Convert from a float to a q31 value, saturating above 1.0
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency. Inline asm: clang does not form the
/// fixed-point `vcvt` by itself.
[[gnu::always_inline]] inline int32_t q31_from_float(float value) {
	asm("vcvt.s32.f32 %0, %0, #31" : "+t"(value));
	return __builtin_bit_cast(int32_t, value);
}

///@brief Convert from a q31 to a float
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
[[gnu::always_inline]] inline float q31_to_float(int32_t value) {
	asm("vcvt.f32.s32 %0, %0, #31" : "+t"(value));
	return __builtin_bit_cast(float, value);
}
#else

[[gnu::always_inline]] inline int32_t multiply_32x32_rshift32(int32_t a, int32_t b) {
	return (int32_t)(((int64_t)a * b) >> 32);
}

/// This multiplies two numbers in signed Q31 fixed point and rounds the result
[[gnu::always_inline]] inline int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b) {
	return (int32_t)(((int64_t)a * b + 0x80000000) >> 32);
}

/// This multiplies two numbers in signed Q31 fixed point, returning the result in q31. Matches the ARM
/// `smmul`-then-`* 2` of the hardware path (smmul = multiply_32x32_rshift32).
[[gnu::always_inline]] inline int32_t q31_mult(int32_t a, int32_t b) {
	return multiply_32x32_rshift32(a, b) * 2;
}

/// This multiplies a number in q31 by a number in q32, returning a scaled value. The ARM path is `smmul`
/// (a signed multiply) even though proportion is unsigned, so match that by treating proportion as signed.
[[gnu::always_inline]] inline int32_t q31t_rescale(int32_t a, uint32_t proportion) {
	return multiply_32x32_rshift32(a, (int32_t)proportion);
}

/// Multiplies A and B, adds to sum, and returns output.
/// The 64-bit product stays separate and `sum` is added last, wrapping in 32 bits as the ARM smmla does:
/// floor((sum<<32 ± a*b)/2^32) = sum ± (a*b>>32), with no int64 overflow for Q31-scale operands.
[[gnu::always_inline]] inline int32_t multiply_accumulate_32x32_rshift32(int32_t sum, int32_t a, int32_t b) {
	return (int32_t)(sum + (((int64_t)a * b) >> 32));
}

/// Multiplies A and B, adds to sum, and returns output, rounding
[[gnu::always_inline]] inline int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b) {
	return (int32_t)(sum + (((int64_t)a * b + 0x80000000) >> 32));
}

/// Multiplies A and B, subtracts from sum, and returns output, rounding
[[gnu::always_inline]] inline int32_t multiply_subtract_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b) {
	return (int32_t)(sum + ((0x80000000LL - (int64_t)a * b) >> 32));
}

/// Matches ARM `ssat %0, %1, #bits`: clamp to the signed range representable in `bits` bits,
/// i.e. [-2^(bits-1), 2^(bits-1)-1].
template <uint8_t bits>
[[gnu::always_inline]] inline int32_t signed_saturate(int32_t val) {
	constexpr int32_t hi = static_cast<int32_t>((static_cast<uint32_t>(1) << (bits - 1)) - 1);
	constexpr int32_t lo = -hi - 1;
	return val > hi ? hi : (val < lo ? lo : val);
}

/// Matches ARM `qadd`: saturating add, clamped to the signed 32-bit range.
[[gnu::always_inline]] inline int32_t add_saturate(int32_t a, int32_t b) {
	int64_t r = static_cast<int64_t>(a) + b;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
}

/// Matches ARM `qsub`: saturating subtract, clamped to the signed 32-bit range.
[[gnu::always_inline]] inline int32_t subtract_saturate(int32_t a, int32_t b) {
	int64_t r = static_cast<int64_t>(a) - b;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
}

[[gnu::always_inline]] inline int32_t clz(uint32_t input) {
	// ARM `clz` returns 32 for 0; __builtin_clz(0) is UB. Match the hardware.
	return input ? __builtin_clz(input) : 32;
}

[[gnu::always_inline]] constexpr int32_t q31_from_float(float value) {
	// NaN converts to 0, as vcvt.s32.f32 gives it.
	if (value != value) {
		return 0;
	}

	// A float is represented as 32 bits:
	// 1-bit sign, 8-bit exponent, 24-bit mantissa

	auto bits = std::bit_cast<uint32_t>(value);

	// Sign bit being 1 indicates negative value
	bool negative = bits & 0x80000000;

	// Extract exponent and adjust for bias (IEEE 754)
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;

	// saturate if |value| >= 1.f (vcvt.s32.f32 #31 saturates to INT32_MIN / INT32_MAX)
	if (exponent >= 0) {
		return negative ? std::numeric_limits<int32_t>::min() : std::numeric_limits<int32_t>::max();
	}

	// |value| < 2^-31 truncates to 0; the shift would be >= the operand width.
	int32_t shift = -exponent;
	if (shift >= 32) {
		return 0;
	}

	// extract mantissa (truncating toward zero, matching VCVT's rounding mode)
	uint32_t mantissa = (bits << 8) | 0x80000000;
	int32_t output_value = static_cast<int32_t>(mantissa >> shift);
	return (negative) ? -output_value : output_value;
}

///@brief Convert from a q31 to a float. Matches the ARM `vcvt.f32.s32 #31` (divide by 2^31).
[[gnu::always_inline]] inline float q31_to_float(int32_t value) {
	return static_cast<float>(value) / 2147483648.0f;
}
#endif

/// bits must be *less* than 32! I.e. 31 or less
[[gnu::always_inline]] inline int32_t signed_saturate_operand_unknown(int32_t val, int32_t bits) {

	// Despite having this switch at the per-audio-sample level, it doesn't introduce any slowdown compared to just
	// always saturating by the same amount!
	switch (bits) {
	case 31:
		return signed_saturate<31>(val);
	case 30:
		return signed_saturate<30>(val);
	case 29:
		return signed_saturate<29>(val);
	case 28:
		return signed_saturate<28>(val);
	case 27:
		return signed_saturate<27>(val);
	case 26:
		return signed_saturate<26>(val);
	case 25:
		return signed_saturate<25>(val);
	case 24:
		return signed_saturate<24>(val);
	case 23:
		return signed_saturate<23>(val);
	case 22:
		return signed_saturate<22>(val);
	case 21:
		return signed_saturate<21>(val);
	case 20:
		return signed_saturate<20>(val);
	case 19:
		return signed_saturate<19>(val);
	case 18:
		return signed_saturate<18>(val);
	case 17:
		return signed_saturate<17>(val);
	case 16:
		return signed_saturate<16>(val);
	case 15:
		return signed_saturate<15>(val);
	case 14:
		return signed_saturate<14>(val);
	case 13:
		return signed_saturate<13>(val);
	default:
		return signed_saturate<12>(val);
	}
}

template <uint8_t lshift>
[[gnu::always_inline]] inline int32_t lshift_and_saturate(int32_t val) {
	return signed_saturate<32 - lshift>(val) << lshift;
}

/// lshift must be greater than 0! Not 0
[[gnu::always_inline]] inline int32_t lshift_and_saturate_unknown(int32_t val, uint8_t lshift) {
	return signed_saturate_operand_unknown(val, 32 - lshift) << lshift;
}

/// Scale `number` by 2^magnitude (negative magnitude = right-shift), saturating on positive magnitude.
[[gnu::always_inline]] inline int32_t increase_magnitude_and_saturate(int32_t number, int32_t magnitude) {
	if (magnitude > 0) {
		return lshift_and_saturate_unknown(number, magnitude);
	}
	return number >> (-magnitude);
}

} // namespace deluge::dsp::detail
