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
#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>

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

// Multiplies A and B, adds to sum, and returns output
static inline q31_t multiply_accumulate_32x32_rshift32(q31_t sum, q31_t a, q31_t b)
    __attribute__((always_inline, unused));
static inline q31_t multiply_accumulate_32x32_rshift32(q31_t sum, q31_t a, q31_t b) {
	q31_t out;
	asm("smmla %0, %1, %2, %3" : "=r"(out) : "r"(a), "r"(b), "r"(sum));
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

static inline int32_t add_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t add_saturate(int32_t a, int32_t b) {
	int32_t out;
	asm("qadd %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

static inline int32_t subtract_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t subtract_saturate(int32_t a, int32_t b) {
	int32_t out;
	asm("qsub %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
	return out;
}

inline int32_t clz(uint32_t input) {
	int32_t out;
	asm("clz %0, %1" : "=r"(out) : "r"(input));
	return out;
}

///@brief Convert from a float to a q31 value, saturating above 1.0
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
static inline q31_t q31_from_float(float value) {
	asm("vcvt.s32.f32 %0, %0, #31" : "+t"(value));
	return std::bit_cast<q31_t>(value);
}

///@brief Convert from a q31 to a float
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
static inline float q31_to_float(q31_t value) {
	asm("vcvt.f32.s32 %0, %0, #31" : "+t"(value));
	return std::bit_cast<float>(value);
}
#else

static inline q31_t multiply_32x32_rshift32(q31_t a, q31_t b) {
	return (int32_t)(((int64_t)a * b) >> 32);
}

// This multiplies two numbers in signed Q31 fixed point and rounds the result
static inline q31_t multiply_32x32_rshift32_rounded(q31_t a, q31_t b) {
	return (int32_t)(((int64_t)a * b + 0x80000000) >> 32);
}

// This multiplies two numbers in signed Q31 fixed point, returning the result in q31. Matches the ARM
// `smmul`-then-`* 2` of the hardware path (smmul = multiply_32x32_rshift32).
static inline q31_t q31_mult(q31_t a, q31_t b) {
	return multiply_32x32_rshift32(a, b) * 2;
}

// This multiplies a number in q31 by a number in q32, returning a scaled value. The ARM path is `smmul`
// (a signed multiply) even though proportion is unsigned, so match that by treating proportion as signed.
static inline q31_t q31tRescale(q31_t a, uint32_t proportion) {
	return multiply_32x32_rshift32(a, (q31_t)proportion);
}

// Multiplies A and B, adds to sum, and returns output.
// NB: keep the 64-bit product term separate and add `sum` last. The old form shifted sum<<32 first
// (~2^63) then added a*b (~2^62), overflowing int64 (UB) for Q31-scale operands — it diverged from the
// ARM smmla (which wraps in 32-bit) and, at -O2, the UB corrupted neighbouring code. Mathematically the
// non-overflowing forms below are identical to the originals: floor((sum<<32 ± a*b)/2^32) = sum ± (a*b>>32).
static inline q31_t multiply_accumulate_32x32_rshift32(q31_t sum, q31_t a, q31_t b) {
	return (q31_t)(sum + (((int64_t)a * b) >> 32));
}

// Multiplies A and B, adds to sum, and returns output, rounding
static inline q31_t multiply_accumulate_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return (q31_t)(sum + (((int64_t)a * b + 0x80000000) >> 32));
}

// Multiplies A and B, subtracts from sum, and returns output
static inline q31_t multiply_subtract_32x32_rshift32(q31_t sum, q31_t a, q31_t b) {
	return (q31_t)(sum + ((-((int64_t)a * b)) >> 32));
}

// Multiplies A and B, subtracts from sum, and returns output, rounding
static inline q31_t multiply_subtract_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return (q31_t)(sum + ((0x80000000LL - (int64_t)a * b) >> 32));
}

// Matches ARM `ssat %0, %1, #bits`: clamp to the signed range representable in `bits` bits,
// i.e. [-2^(bits-1), 2^(bits-1)-1]. (The old host stub `std::min(val, 1<<bits)` clamped only the
// upper bound, to the wrong value, with no lower bound — silently wrong saturation in the DSP.)
template <uint8_t bits>
static inline int32_t signed_saturate(int32_t val) {
	constexpr int32_t hi = static_cast<int32_t>((static_cast<uint32_t>(1) << (bits - 1)) - 1);
	constexpr int32_t lo = -hi - 1;
	return val > hi ? hi : (val < lo ? lo : val);
}

// Matches ARM `qadd`: saturating add, clamped to the signed 32-bit range (the old host stub
// `a + b` wrapped on overflow — a ramp past INT32_MAX flipped to negative instead of pinning).
static inline int32_t add_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t add_saturate(int32_t a, int32_t b) {
	int64_t r = static_cast<int64_t>(a) + b;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
}

// Matches ARM `qsub`: saturating subtract, clamped to the signed 32-bit range.
static inline int32_t subtract_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t subtract_saturate(int32_t a, int32_t b) {
	int64_t r = static_cast<int64_t>(a) - b;
	return r > INT32_MAX ? INT32_MAX : (r < INT32_MIN ? INT32_MIN : static_cast<int32_t>(r));
}

inline int32_t clz(uint32_t input) {
	// ARM `clz` returns 32 for 0; __builtin_clz(0) is UB. Match the hardware.
	return input ? __builtin_clz(input) : 32;
}

[[gnu::always_inline]] constexpr q31_t q31_from_float(float value) {
	// A float is represented as 32 bits:
	// 1-bit sign, 8-bit exponent, 24-bit mantissa

	auto bits = std::bit_cast<uint32_t>(value);

	// Sign bit being 1 indicates negative value
	bool negative = bits & 0x80000000;

	// Extract exponent and adjust for bias (IEEE 754)
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;

	// saturate if |value| >= 1.f (vcvt.s32.f32 #31 saturates to INT32_MIN / INT32_MAX)
	if (exponent >= 0) {
		return negative ? std::numeric_limits<q31_t>::min() : std::numeric_limits<q31_t>::max();
	}

	// |value| < 2^-31 truncates to 0. (Was `mantissa >> -exponent` — for these tiny values -exponent
	// is >= 32, i.e. a shift >= the operand width, which is UB and at -O2 corrupted neighbouring code.)
	int32_t shift = -exponent;
	if (shift >= 32) {
		return 0;
	}

	// extract mantissa (truncating toward zero, matching VCVT's rounding mode)
	uint32_t mantissa = (bits << 8) | 0x80000000;
	q31_t output_value = static_cast<q31_t>(mantissa >> shift);
	return (negative) ? -output_value : output_value;
}

///@brief Convert from a q31 to a float. Matches the ARM `vcvt.f32.s32 #31` (divide by 2^31).
static inline float q31_to_float(q31_t value) {
	return static_cast<float>(value) / 2147483648.0f;
}
#endif

/// @brief Fixed point number with a configurable number of fractional bits
/// @note This class only supports 32-bit signed fixed point numbers
/// @tparam FractionalBits The number of fractional bits
/// @tparam Rounded Whether to round the result when performing operations
/// @tparam FastApproximation Whether to use a fast approximation for operations
template <std::size_t FractionalBits, bool Rounded = false, bool FastApproximation = true>
class FixedPoint {
	static_assert(FractionalBits > 0, "FractionalBits must be greater than 0");
	static_assert(FractionalBits < 32, "FractionalBits must be less than 32");

	using BaseType = int32_t;
	using IntermediateType = int64_t;

	/// a + b * c
	[[gnu::always_inline]] static int32_t signed_most_significant_word_multiply_add(int32_t a, int32_t b, int32_t c) {
		if constexpr (Rounded) {
			return multiply_accumulate_32x32_rshift32_rounded(a, b, c);
		}
		else {
			return multiply_accumulate_32x32_rshift32(a, b, c);
		}
	}

	// a * b
	[[gnu::always_inline]] static int32_t signed_most_significant_word_multiply(int32_t a, int32_t b) {
		if constexpr (Rounded) {
			return multiply_32x32_rshift32_rounded(a, b);
		}
		else {
			return multiply_32x32_rshift32(a, b);
		}
	}

	static constexpr BaseType one() noexcept {
		if constexpr (fractional_bits == 31) {
			return std::numeric_limits<BaseType>::max();
		}
		else {
			return 1 << fractional_bits;
		}
	}

public:
	constexpr static std::size_t fractional_bits = FractionalBits;
	constexpr static std::size_t integral_bits = 32 - FractionalBits;
	constexpr static bool rounded = Rounded;
	constexpr static bool fast_approximation = FastApproximation;

	// --- Portable emulation of the ARM VFP fixed-point conversion instructions ---
	//
	// vcvt.{s32.f32,f32.s32,s32.f64,f64.s32} back the float/double conversions on ARM. Off-target
	// (no VFP) and during constant evaluation we must reproduce them *bit-exactly*, or the host-sim
	// diverges from the firmware. The hardware behaviour is: scale by exactly 2^fractional_bits,
	// round toward zero, saturate to the int32 range, and map NaN to 0. Verified lane-for-lane
	// against qemu-arm in tests/qemu/spec/fixedpoint_vfp_spec.cpp.
	//
	// Note the scale is 2^fractional_bits, NOT one() — one() is INT32_MAX (not 2^31) when
	// fractional_bits == 31, whereas VFP always uses the power of two. 2^fractional_bits is exact
	// in double for every valid fractional_bits, so the scaling never rounds.
	static constexpr double vfp_scale() noexcept { return static_cast<double>(uint64_t{1} << fractional_bits); }

	/// Round toward zero and saturate a scaled real value to int32, as VFP's f->fixed convert does.
	static constexpr BaseType saturate_to_raw(double scaled) noexcept {
		// INT32_MAX + 1 == 2^31 and INT32_MIN - 1 == -(2^31)-1, both exact in double. +inf/-inf
		// compare past these bounds and saturate, matching the hardware.
		if (scaled >= static_cast<double>(std::numeric_limits<BaseType>::max()) + 1.0) {
			return std::numeric_limits<BaseType>::max();
		}
		if (scaled <= static_cast<double>(std::numeric_limits<BaseType>::min()) - 1.0) {
			return std::numeric_limits<BaseType>::min();
		}
		return static_cast<BaseType>(scaled); // double->int truncates toward zero; guaranteed in range here
	}

	/// vcvt.s32.f32 / vcvt.s32.f64: floating point -> fixed point. (float promotes to double exactly.)
	static constexpr BaseType float_to_raw(double value) noexcept {
		if (value != value) { // NaN -> 0
			return 0;
		}
		return saturate_to_raw(value * vfp_scale());
	}

	/// vcvt.f32.s32: fixed point -> float (a single round-to-nearest, as the hardware does).
	static constexpr float raw_to_float(BaseType raw) noexcept {
		return static_cast<float>(static_cast<double>(raw) / vfp_scale());
	}

	/// vcvt.f64.s32: fixed point -> double (exact).
	static constexpr double raw_to_double(BaseType raw) noexcept { return static_cast<double>(raw) / vfp_scale(); }

	/// @brief Default constructor
	constexpr FixedPoint() = default;

	/// @brief Construct a fixed point number from another fixed point number
	/// @note This will saturate or truncate (and/or round) the value if the number of fractional bits is different
	template <std::size_t OtherFractionalBits>
	constexpr explicit FixedPoint(FixedPoint<OtherFractionalBits> other) noexcept : value_(other.raw()) {
		if constexpr (FractionalBits == OtherFractionalBits) {
			return;
		}
		else if constexpr (FractionalBits > OtherFractionalBits) {
			// saturate
			constexpr int32_t shift = FractionalBits - OtherFractionalBits;
			value_ = signed_saturate<32 - shift>(value_);
			value_ = (value_ << shift) + (value_ % 2);
		}
		else if constexpr (rounded) {
			// round
			constexpr int32_t shift = OtherFractionalBits - FractionalBits;
			value_ >>= shift + ((1 << shift) - 1);
		}
		else {
			// truncate
			value_ >>= (OtherFractionalBits - FractionalBits);
		}
	}

	/// @brief Convert an integer to a fixed point number
	/// @note This truncates the integer if it is too large
	template <std::integral T>
	constexpr explicit FixedPoint(T value) noexcept : value_(static_cast<BaseType>(value) << fractional_bits) {}

	/// @brief Convert from a float to a fixed point number
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	constexpr explicit FixedPoint(float value) noexcept {
#if defined(__arm__)
		// Note: a plain (not `if constexpr`) runtime check — `if constexpr (std::is_constant_evaluated())`
		// is always true, which would discard the VFP path entirely. On host there is no VFP, so the asm
		// branch is compiled out and the portable path below is always taken.
		if (!std::is_constant_evaluated()) {
			asm("vcvt.s32.f32 %0, %0, %1" : "+t"(value) : "I"(fractional_bits));
			value_ = std::bit_cast<int32_t>(value); // NOLINT
			return;
		}
#endif
		value_ = float_to_raw(value);
	}

	/// @brief Explicit conversion to float
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	constexpr explicit operator float() const noexcept {
#if defined(__arm__)
		if (!std::is_constant_evaluated()) {
			int32_t output = value_;
			asm("vcvt.f32.s32 %0, %0, %1" : "+t"(output) : "I"(fractional_bits));
			return std::bit_cast<float>(output);
		}
#endif
		return raw_to_float(value_);
	}

	/// @brief Convert from a double to a fixed point number
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	constexpr explicit FixedPoint(double value) noexcept {
#if defined(__arm__)
		if (!std::is_constant_evaluated()) {
			auto output = std::bit_cast<int64_t>(value);
			// %P selects the 64-bit d-register name (the fixed-point vcvt.*.f64 operate on a d-reg; a
			// bare %0 prints the single-precision s-name). The instruction is in-place — operands must
			// be the same register — so use a single read-write (+w) operand rather than tied =w/w.
			asm("vcvt.s32.f64 %P0, %P0, %1" : "+w"(output) : "I"(fractional_bits));
			value_ = static_cast<BaseType>(output);
			return;
		}
#endif
		value_ = float_to_raw(value);
	}

	/// @brief Explicit conversion to double
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	explicit operator double() const noexcept {
#if defined(__arm__)
		if (!std::is_constant_evaluated()) {
			auto output = std::bit_cast<double>((int64_t)value_);
			asm("vcvt.f64.s32 %P0, %P0, %1" : "+w"(output) : "I"(fractional_bits));
			return output;
		}
#endif
		return raw_to_double(value_);
	}

	/// @brief Convert to a fixed point number with a different number of fractional bits
	template <std::size_t OutputFractionalBits>
	constexpr FixedPoint<OutputFractionalBits> as() const {
		return static_cast<FixedPoint<OutputFractionalBits>>(*this);
	}

	/// @brief Explicit conversion to integer
	template <std::integral T>
	explicit operator T() const noexcept {
		return static_cast<T>(value_ >> fractional_bits);
	}

	explicit operator bool() const noexcept { return value_ != 0; }

	/// @brief Negation operator
	constexpr FixedPoint operator-() const { return FixedPoint::from_raw(-value_); }

	/// @brief Get the internal value
	[[nodiscard]] constexpr BaseType raw() const noexcept { return value_; }

	/// @brief Construct from a raw value
	static constexpr FixedPoint from_raw(BaseType raw) noexcept {
		FixedPoint result{};
		result.value_ = raw;
		return result;
	}

	/// @brief Addition operator
	/// Add two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator+(const FixedPoint& rhs) const { return from_raw(add_saturate(value_, rhs.value_)); }

	/// @brief Addition operator
	/// Add two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator+=(const FixedPoint& rhs) {
		value_ = add_saturate(value_, rhs.value_);
		return *this;
	}

	/// @brief Subtraction operator
	/// Subtract two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator-(const FixedPoint& rhs) const {
		return from_raw(subtract_saturate(value_, rhs.value_));
	}

	/// @brief Subtraction operator
	/// Subtract two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator-=(const FixedPoint& rhs) {
		value_ = subtract_saturate(value_, rhs.value_);
		return *this;
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator*(const FixedPoint& rhs) const {
		if constexpr (fast_approximation && fractional_bits > 16) {
			// less than 16 would mean no fractional bits remain after right shift by 32
			constexpr int32_t shift = fractional_bits - ((fractional_bits * 2) - 32);
			return from_raw(signed_most_significant_word_multiply(value_, rhs.value_) << shift);
		}

		if constexpr (rounded) {
			IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.value_) >> (fractional_bits - 1);
			return from_raw(static_cast<BaseType>((value >> 1) + (value % 2)));
		}

		IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.value_) >> fractional_bits;
		return from_raw(static_cast<BaseType>(value));
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator*=(const FixedPoint& rhs) {
		value_ = this->operator*(rhs).value_;
		return *this;
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with different number of fractional bits
	template <std::size_t OutputFractionalBits = FractionalBits, std::size_t OtherFractionalBits, bool OtherRounded,
	          bool OtherApproximating>
	requires(OtherRounded == Rounded && OtherApproximating == FastApproximation)
	constexpr FixedPoint<OutputFractionalBits, Rounded, FastApproximation>
	operator*(const FixedPoint<OtherFractionalBits, OtherRounded, OtherApproximating>& rhs) const {
		if constexpr (fast_approximation) {
			constexpr int32_t l_shift = OutputFractionalBits - ((FractionalBits + OtherFractionalBits) - 32);
			static_assert(l_shift < 32 && l_shift > -32);
			BaseType value = signed_most_significant_word_multiply(value_, rhs.raw());
			return from_raw(l_shift > 0 ? value << l_shift : value >> -l_shift);
		}

		constexpr int32_t r_shift = (FractionalBits + OtherFractionalBits) - OutputFractionalBits;
		if constexpr (rounded) {
			IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.raw())
			                         >> (r_shift - 1); // At this point Q is FractionalBits + OtherFractionalBits
			return from_raw(static_cast<BaseType>((value / 2) + (value % 2)));
		}

		IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.raw()) >> r_shift;
		return from_raw(static_cast<BaseType>(value));
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with different number of fractional bits
	template <std::size_t OtherFractionalBits>
	constexpr FixedPoint operator*=(const FixedPoint<OtherFractionalBits>& rhs) {
		value_ = this->operator*(rhs).value_;
		return *this;
	}

	/// @brief Division operator
	/// Divide two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator/(const FixedPoint& rhs) const {
		if (rhs.value_ == 0) {
			return from_raw(std::numeric_limits<BaseType>::max());
		}
		if constexpr (rounded) {
			IntermediateType value = (static_cast<IntermediateType>(value_) << (fractional_bits + 1)) / rhs.value_;
			return from_raw(static_cast<BaseType>((value / 2) + (value % 2)));
		}

		IntermediateType value = (static_cast<IntermediateType>(value_) << fractional_bits) / rhs.value_;
		return from_raw(static_cast<BaseType>(value));
	}

	/// @brief Division operator
	/// Divide two fixed point numbers with the same number of fractional bits
	constexpr FixedPoint operator/=(const FixedPoint& rhs) {
		value_ = this->operator/(rhs).value_;
		return *this;
	}

	/// @brief Fused multiply-add operation for fixed point numbers with a different number of fractional bits
	template <std::size_t OtherFractionalBitsA, std::size_t OtherFractionalBitsB>
	constexpr FixedPoint MultiplyAdd(const FixedPoint<OtherFractionalBitsA>& a,
	                                 const FixedPoint<OtherFractionalBitsB>& b) const {
		// ensure that the number of fractional bits in the addend is equal to the sum of the number of fractional bits
		// in multiplicands, minus 32 (due to right shift) before using smmla/smmlar
		if constexpr (fast_approximation && (OtherFractionalBitsA + OtherFractionalBitsB) - 32 == FractionalBits) {
			return FixedPoint::from_raw(signed_most_significant_word_multiply_add(value_, a.raw(), b.raw()));
		}
		return *this + static_cast<FixedPoint>(a * b);
	}

	/// @brief Fused multiply-add operation for fixed point numbers with the same number of fractional bits
	constexpr FixedPoint MultiplyAdd(const FixedPoint& a, const FixedPoint& b) const {
		if constexpr (fast_approximation && (((FractionalBits * 2) - 32) == (FractionalBits - 1))) {
			return from_raw(static_cast<FixedPoint<FractionalBits - 1>>(*this).MultiplyAdd(a, b).raw() << 1);
		}
		return *this + (a * b);
	}

	/// @brief Equality operator
	constexpr bool operator==(const FixedPoint& rhs) const noexcept { return value_ == rhs.value_; }

	/// @brief Three-way comparison operator
	constexpr std::strong_ordering operator<=>(const FixedPoint& rhs) const noexcept { return value_ <=> rhs.value_; }

	/// @brief Equality operator for fixed point numbers with different number of fractional bits
	template <std::size_t OtherFractionalBits>
	constexpr bool operator==(const FixedPoint<OtherFractionalBits>& rhs) const noexcept {
		int integral_value = value_ >> fractional_bits;
		int other_integral_value = rhs.raw() >> OtherFractionalBits;
		int fractional_value = value_ & ((1 << fractional_bits) - 1);              // Mask out the integral part
		int other_fractional_value = rhs.raw() & ((1 << OtherFractionalBits) - 1); // Mask out the integral parts

		// Shift the fractional part if the number of fractional bits is different
		if (fractional_bits > OtherFractionalBits) {
			fractional_value >>= fractional_bits - OtherFractionalBits;
		}
		else {
			other_fractional_value >>= OtherFractionalBits - fractional_bits;
		}

		return integral_value == other_integral_value && fractional_value == other_fractional_value;
	}

	/// @brief Three-way comparison operator for fixed point numbers with different number of fractional bits
	template <std::size_t OtherFractionalBits>
	constexpr std::strong_ordering operator<=>(const FixedPoint<OtherFractionalBits>& rhs) const noexcept {
		// Compare integral and fractional components separately
		int integral_value = value_ >> fractional_bits;
		int other_integral_value = rhs.raw() >> OtherFractionalBits;
		int fractional_value = value_ & ((1 << fractional_bits) - 1);              // Mask out the integral part
		int other_fractional_value = rhs.raw() & ((1 << OtherFractionalBits) - 1); // Mask out the integral part

		// Shift the fractional part if the number of fractional bits is different
		if (fractional_bits > OtherFractionalBits) {
			fractional_value >>= fractional_bits - OtherFractionalBits;
		}
		else {
			other_fractional_value >>= OtherFractionalBits - fractional_bits;
		}

		if (integral_value < other_integral_value) {
			return std::strong_ordering::less;
		}
		if (integral_value > other_integral_value) {
			return std::strong_ordering::greater;
		}
		if (fractional_value < other_fractional_value) {
			return std::strong_ordering::less;
		}
		if (fractional_value > other_fractional_value) {
			return std::strong_ordering::greater;
		}
		return std::strong_ordering::equal;
	}

	/// @brief Equality operator for integers and floating point numbers
	template <typename T>
	requires std::integral<T> || std::floating_point<T>
	constexpr bool operator==(const T& rhs) const noexcept {
		return value_ == FixedPoint(rhs).value_;
	}

private:
	BaseType value_{0};
};
