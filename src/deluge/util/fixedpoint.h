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

// Effective 0dBFS reference for headroom calculations
// This represents 2^24 = 16,777,216 which gives ~24dB of headroom
constexpr int32_t EFFECTIVE_0DBFS_Q31 = ONE_Q31 / 128; // 2^24 = 16,777,216
constexpr float EFFECTIVE_0DBFS_Q31f = static_cast<float>(EFFECTIVE_0DBFS_Q31);

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
	asm("vcvt.s32.f32 %0, %0, #31" : "=t"(value) : "t"(value));
	return std::bit_cast<q31_t>(value);
}

///@brief Convert from a q31 to a float
///@note VFP instruction - 1 cycle for issue, 4 cycles result latency
static inline float q31_to_float(q31_t value) {
	asm("vcvt.f32.s32 %0, %0, #31" : "=t"(value) : "t"(value));
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

// Multiplies A and B, adds to sum, and returns output
static inline q31_t multiply_accumulate_32x32_rshift32(q31_t sum, q31_t a, q31_t b) {
	return (int32_t)(((((int64_t)sum) << 32) + ((int64_t)a * b)) >> 32);
}

// Multiplies A and B, adds to sum, and returns output, rounding
static inline q31_t multiply_accumulate_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return (int32_t)(((((int64_t)sum) << 32) + ((int64_t)a * b) + 0x80000000) >> 32);
}

// Multiplies A and B, subtracts from sum, and returns output
static inline q31_t multiply_subtract_32x32_rshift32(q31_t sum, q31_t a, q31_t b) {
	return (int32_t)(((((int64_t)sum) << 32) - ((int64_t)a * b)) >> 32);
}

// Multiplies A and B, subtracts from sum, and returns output, rounding
static inline q31_t multiply_subtract_32x32_rshift32_rounded(q31_t sum, q31_t a, q31_t b) {
	return (int32_t)((((((int64_t)sum) << 32) - ((int64_t)a * b)) + 0x80000000) >> 32);
}

// computes limit((val >> rshift), 2**bits)
template <uint8_t bits>
static inline int32_t signed_saturate(int32_t val) {
	return std::min(val, 1 << bits);
}

static inline int32_t add_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t add_saturate(int32_t a, int32_t b) {
	return a + b;
}

static inline int32_t subtract_saturate(int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t subtract_saturate(int32_t a, int32_t b) {
	return a - b;
}

inline int32_t clz(uint32_t input) {
	return __builtin_clz(input);
}

[[gnu::always_inline]] constexpr q31_t q31_from_float(float value) {
	// A float is represented as 32 bits:
	// 1-bit sign, 8-bit exponent, 24-bit mantissa

	auto bits = std::bit_cast<uint32_t>(value);

	// Sign bit being 1 indicates negative value
	bool negative = bits & 0x80000000;

	// Extract exponent and adjust for bias (IEEE 754)
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;

	q31_t output_value = 0;

	// saturate if above 1.f
	if (exponent >= 0) {
		output_value = std::numeric_limits<q31_t>::max();
	}

	// extract mantissa
	else {
		uint32_t mantissa = (bits << 8) | 0x80000000;
		output_value = static_cast<q31_t>(mantissa >> -exponent);
	}

	// Sign bit
	return (negative) ? -output_value : output_value;
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
		if constexpr (std::is_constant_evaluated()) {
			value *= FixedPoint::one();
			// convert from floating-point to fixed point
			if constexpr (rounded) {
				value_ = static_cast<BaseType>((value >= 0.0) ? std::ceil(value) : std::floor(value));
			}
			else {
				value_ = static_cast<BaseType>(value);
			}
		}
		else {
			asm("vcvt.s32.f32 %0, %1, %2" : "=t"(value) : "t"(value), "I"(fractional_bits));
			value_ = std::bit_cast<int32_t>(value); // NOLINT
		}
	}

	/// @brief Explicit conversion to float
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	constexpr explicit operator float() const noexcept {
		if constexpr (std::is_constant_evaluated()) {
			return static_cast<float>(value_) / FixedPoint::one();
		}
		int32_t output = value_;
		asm("vcvt.f32.s32 %0, %1, %2" : "=t"(output) : "t"(output), "I"(fractional_bits));
		return std::bit_cast<float>(output);
	}

	/// @brief Convert from a double to a fixed point number
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	constexpr explicit FixedPoint(double value) noexcept {
		if constexpr (std::is_constant_evaluated()) {
			value *= FixedPoint::one();
			// convert from floating-point to fixed point
			if constexpr (rounded) {
				value_ = static_cast<BaseType>((value >= 0.0) ? std::ceil(value) : std::floor(value));
			}
			else {
				value_ = static_cast<BaseType>(value);
			}
		}
		else {
			auto output = std::bit_cast<int64_t>(value);
			asm("vcvt.s32.f64 %0, %1, %2" : "=w"(output) : "w"(output), "I"(fractional_bits));
			value_ = static_cast<BaseType>(output);
		}
	}

	/// @brief Explicit conversion to double
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	explicit operator double() const noexcept {
		if constexpr (std::is_constant_evaluated()) {
			return static_cast<double>(value_) / FixedPoint::one();
		}
		auto output = std::bit_cast<double>((int64_t)value_);
		asm("vcvt.f64.s32 %0, %1, %2" : "=w"(output) : "w"(output), "I"(fractional_bits));
		return output;
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
