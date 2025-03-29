/**
 * Copyright (c) 2025 Katherine Whitlock
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

#include <algorithm>
#include <cstdint>
#include <type_traits>

#ifdef __GNUC__
#ifdef __clang__
#define COMPILER_CLANG 1
#define COMPILER_GCC 0
#else
#define COMPILER_CLANG 0
#define COMPILER_GCC 1
#endif
#endif

#ifndef __ARM_ARCH
#define __ARM_ARCH 0
#endif

#ifndef __ARM_ARCH_7A__
#define __ARM_ARCH_7A__ 0
#endif

constexpr bool ARMv7a = (__ARM_ARCH == 7 && __ARM_ARCH_7A__);
constexpr bool kCompilerClang = COMPILER_CLANG;

// This multiplies two numbers in signed Q31 fixed point as if they were q32, so the return value is half what it should
// be. Use this when several corrective shifts can be accumulated and then combined
[[gnu::always_inline]] static constexpr int32_t multiply_32x32_rshift32(int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a || kCompilerClang) {
		return (int32_t)(((int64_t)a * b) >> 32);
	}
}

// This multiplies two numbers in signed Q31 fixed point and rounds the result
[[gnu::always_inline]] static constexpr int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a || kCompilerClang) {
		return (int32_t)(((int64_t)a * b + 0x80000000) >> 32);
	}
	else {
		int32_t out;
		asm("smmulr %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
		return out;
	}
}

/// @brief This multiplies two numbers in signed Q31 fixed point, returning the result in Q31.
/// @deprecated This function is deprecated. Use FixedPoint<31>::multiply instead.
/// @note deprecated because the two branches perform different operations and cannot be relied on for testing
[[gnu::always_inline]] static constexpr int32_t q31_mult(int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return (int32_t)(((int64_t)a * b) >> 31);
	}
	else {
		return multiply_32x32_rshift32(a, b) << 1;
	}
}

// Multiplies A and B, adds to sum, and returns output
[[gnu::always_inline]] static constexpr int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a,
                                                                                           int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a || kCompilerClang) {
		return (int32_t)(((((int64_t)sum) << 32) + ((int64_t)a * b) + 0x80000000) >> 32);
	}
	else {
		int32_t out;
		asm("smmlar %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
		return out;
	}
}

// Multiplies A and B, adds to sum, and returns output
[[gnu::always_inline]] static constexpr int32_t multiply_accumulate_32x32_rshift32(int32_t sum, int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a || kCompilerClang) {
		return (int32_t)(((((int64_t)sum) << 32) + ((int64_t)a * b)) >> 32);
	}
	else {
		int32_t out;
		asm("smmla %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
		return out;
	}
}

// Multiplies A and B, subtracts from sum, and returns output
[[gnu::always_inline]] static constexpr int32_t multiply_subtract_32x32_rshift32_rounded(int32_t sum, int32_t a,
                                                                                         int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a || kCompilerClang) {
		return (int32_t)((((((int64_t)sum) << 32) - ((int64_t)a * b)) + 0x80000000) >> 32);
	}
	else {
		int32_t out;
		asm("smmlsr %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
		return out;
	}
}

// computes limit((val >> rshift), 2**bits)
template <size_t bits>
requires(bits < 32)
[[gnu::always_inline]] static constexpr int32_t signed_saturate(int32_t val) {
	/// https://developer.arm.com/documentation/ddi0406/c/Application-Level-Architecture/Instruction-Details/Alphabetical-list-of-instructions/SSAT
	return std::clamp<int32_t>(val, -(1LL << (bits - 1)), (1LL << (bits - 1)) - 1);

	/// GCC and Clang both properly reduce this to a single SSAT instruction, so there's no need to use inline asm
	/// See: https://gcc.godbolt.org/z/e5chEzej6
}

template <size_t bits>
requires(bits <= 32)
[[gnu::always_inline]] static constexpr uint32_t unsigned_saturate(uint32_t val) {
	/// https://developer.arm.com/documentation/ddi0406/c/Application-Level-Architecture/Instruction-Details/Alphabetical-list-of-instructions/USAT
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return std::clamp<uint32_t>(val, 0u, (1uL << bits) - 1);
	}
	else {
		uint32_t out;
		asm("usat %0, %1, %2" : "=r"(out) : "I"(bits), "r"(val));
		return out;
	}
}

template <size_t shift, size_t bits = 32>
requires(shift < 32 && bits <= 32)
[[gnu::always_inline]] static constexpr int32_t shift_left_saturate(int32_t val) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return std::clamp<int64_t>((int64_t)val << shift, -(1LL << (bits - 1)), (1LL << (bits - 1)) - 1);
	}
	else {
		int32_t out;
		asm("ssat %0, %1, %2, LSL %3" : "=r"(out) : "I"(bits), "r"(val), "I"(shift));
		return out;
	}
}

template <size_t shift, size_t bits = 32>
requires(shift < 32 && bits <= 32)
[[gnu::always_inline]] static constexpr uint32_t shift_left_saturate(uint32_t val) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return std::clamp<uint64_t>((uint64_t)val << shift, 0u, (1uLL << bits) - 1);
	}
	else {
		uint32_t out;
		asm("usat %0, %1, %2, LSL %3" : "=r"(out) : "I"(bits), "r"(val), "I"(shift));
		return out;
	}
}

[[gnu::always_inline]] static constexpr int32_t add_saturate(int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return std::clamp<int64_t>(a + b, -(1LL << 31), (1LL << 31) - 1);
	}
	else {
		int32_t out;
		asm("qadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
		return out;
	}
}

[[gnu::always_inline]] static constexpr int32_t subtract_saturate(int32_t a, int32_t b) {
	if constexpr (std::is_constant_evaluated() || !ARMv7a) {
		return std::clamp<int64_t>(a - b, -(1LL << 31), (1LL << 31) - 1);
	}
	else {
		int32_t out;
		asm("qsub %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
		return out;
	}
}

/// clz removed as std::countl_zero reduces to the same instruction
/// See: https://gcc.godbolt.org/z/xzns6neaq

///@brief Convert from a float to a q31 value, saturating above 1.0
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
///@deprecated This function is deprecated. Use FixedPoint<31>::FixedPoint instead.
static inline int32_t q31_from_float(float value) {
	asm("vcvt.s32.f32 %0, %0, #31" : "=t"(value) : "t"(value));
	return std::bit_cast<int32_t>(value);
}

///@brief Convert from a q31 to a float
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
///@deprecated This function is deprecated. Use FixedPoint<31>::to_float instead.
static inline float int32_to_float(int32_t value) {
	asm("vcvt.f32.s32 %0, %0, #31" : "=t"(value) : "t"(value));
	return std::bit_cast<float>(value);
}
