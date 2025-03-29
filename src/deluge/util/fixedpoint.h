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

#include "intrinsics.h"
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <limits>

using q31_t = int32_t;

template <size_t bit, typename T>
requires(bit > 0 && bit < 32)
[[gnu::always_inline]] [[nodiscard]] constexpr T roundToBit(T value) {
	return value + (T{1} << bit) - T{1};
}

// This converts the range -2^31 to 2^31 to the range 0-2^31
[[gnu::always_inline]] static inline int32_t toPositive(int32_t a) {
	return ((a / 2) + (1073741824));
}

/// @brief Fixed point number with a configurable number of fractional bits
/// @note This class only supports 32-bit signed fixed point numbers
/// @tparam FractionalBits The number of fractional bits
/// @tparam Rounded Whether to round the result when performing operations
/// @tparam FastApproximation Whether to use a fast approximation for operations
template <std::size_t FractionalBits, bool Rounded = true, bool FastApproximation = (FractionalBits == 31 && ARMv7a)>
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

public:
	static consteval uint32_t one() noexcept {
		return 1 << FractionalBits; // 1.0 in fixed point representation
	}

	constexpr static std::size_t fractional_bits = FractionalBits;
	constexpr static std::size_t integral_bits = 32 - FractionalBits;
	constexpr static bool rounded = Rounded;
	constexpr static bool fast_approximation = FastApproximation;

	constexpr static FixedPoint max() noexcept { return FixedPoint::from_raw(std::numeric_limits<BaseType>::max()); }
	constexpr static FixedPoint min() noexcept { return FixedPoint::from_raw(std::numeric_limits<BaseType>::min()); }

	/// @brief Default constructor
	constexpr FixedPoint() = default;

	/// @brief Construct a fixed point number from another fixed point number
	/// @note This will saturate or truncate (and/or round) the value if the number of fractional bits is different
	template <std::size_t OtherFractionalBits>
	[[gnu::always_inline]] constexpr explicit FixedPoint(FixedPoint<OtherFractionalBits> other) noexcept
	    : value_(other.raw()) {
		if constexpr (FractionalBits == OtherFractionalBits) {
			return;
		}
		else if constexpr (FractionalBits > OtherFractionalBits) {
			// saturate (shift left)
			constexpr int32_t shift = FractionalBits - OtherFractionalBits;
			value_ = signed_saturate<32 - shift>(value_);
			value_ = (value_ << shift) + (value_ % 2);
		}
		else if constexpr (rounded) {
			// round (shift right)
			constexpr int32_t shift = OtherFractionalBits - FractionalBits;
			value_ = roundToBit<shift>(value_) >> shift;
		}
		else {
			// truncate (shift right)
			value_ >>= (OtherFractionalBits - FractionalBits);
		}
	}

	template <std::size_t OtherFractionalBits>
	constexpr operator FixedPoint<OtherFractionalBits>() const {
		return FixedPoint<OtherFractionalBits>{*this};
	}

	/// @brief Convert an integer to a fixed point number
	/// @note This truncates the integer if it is too large
	template <std::integral T>
	constexpr explicit FixedPoint(T value) noexcept : value_(static_cast<BaseType>(value) << fractional_bits) {}

	/// @brief Convert from a float to a fixed point number
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	[[gnu::always_inline]] constexpr FixedPoint(float value) noexcept {
		if constexpr (std::is_constant_evaluated() || !ARMv7a) {
			value *= static_cast<double>(FixedPoint::one());
			// convert from floating-point to fixed point
			if constexpr (rounded) {
				value = std::round(value);
			}

			// saturate
			value_ = static_cast<BaseType>(std::clamp<int64_t>(static_cast<int64_t>(value),
			                                                   std::numeric_limits<BaseType>::min(),
			                                                   std::numeric_limits<BaseType>::max()));
		}
		else {
			asm("vcvt.s32.f32 %0, %1, %2" : "=t"(value) : "t"(value), "I"(fractional_bits));
			value_ = std::bit_cast<int32_t>(value); // NOLINT
		}
	}

	template <std::size_t OtherFractionalBits, bool OtherRounded = Rounded, bool OtherApproximating = FastApproximation>
	constexpr operator FixedPoint<OtherFractionalBits, OtherRounded, OtherApproximating>() const {
		return FixedPoint<OtherFractionalBits, OtherRounded, OtherApproximating>{*this};
	}

	/// @brief Explicit conversion to float
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	[[gnu::always_inline]] explicit constexpr operator float() const noexcept { return to_float(); }

	/// @brief Explicit conversion to float
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	[[gnu::always_inline]] [[nodiscard]] constexpr float to_float() const noexcept {
		if constexpr (std::is_constant_evaluated() || !ARMv7a) {
			return static_cast<float>(value_) / FixedPoint::one();
		}
		else {
			int32_t output = value_;
			asm("vcvt.f32.s32 %0, %1, %2" : "=t"(output) : "t"(output), "I"(fractional_bits));
			return std::bit_cast<float>(output);
		}
	}

	/// @brief Convert from a double to a fixed point number
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	[[gnu::always_inline]] constexpr FixedPoint(double value) noexcept {
		if constexpr (std::is_constant_evaluated() || !ARMv7a) {
			value *= FixedPoint::one();
			// convert from floating-point to fixed point
			if constexpr (rounded) {
				value = std::round(value);
			}

			// saturate
			value_ = static_cast<BaseType>(std::clamp<int64_t>(static_cast<int64_t>(value),
			                                                   std::numeric_limits<BaseType>::min(),
			                                                   std::numeric_limits<BaseType>::max()));
		}
		else {
			auto output = std::bit_cast<int64_t>(value);
			asm("vcvt.s32.f64 %0, %1, %2" : "=w"(output) : "w"(output), "I"(fractional_bits));
			value_ = static_cast<BaseType>(output);
		}
	}

	/// @brief Explicit conversion to double
	/// @note VFP instruction - 1 cycle for issue, 4 cycles result latency
	[[gnu::always_inline]] explicit operator double() const noexcept {
		if constexpr (std::is_constant_evaluated() || !ARMv7a) {
			return static_cast<double>(value_) / FixedPoint::one();
		}
		else {
			auto output = std::bit_cast<double>((int64_t)value_);
			asm("vcvt.f64.s32 %0, %1, %2" : "=w"(output) : "w"(output), "I"(fractional_bits));
			return output;
		}
	}

	/// @brief Convert to a fixed point number with a different number of fractional bits
	template <std::size_t OutputFractionalBits>
	[[gnu::always_inline]] constexpr FixedPoint<OutputFractionalBits> as() const {
		return static_cast<FixedPoint<OutputFractionalBits>>(*this);
	}

	/// @brief Explicit conversion to integer
	template <std::signed_integral T>
	constexpr explicit operator T() const noexcept {
		if constexpr (rounded) {
			// round to nearest integer
			return static_cast<T>(roundToBit<fractional_bits>(value_) >> fractional_bits);
		}
		else {
			// return the integral
			return integral();
		}
	}

	constexpr explicit operator bool() const noexcept { return value_ != 0; }

	/// @brief Negation operator
	[[gnu::always_inline]] [[nodiscard]] constexpr FixedPoint operator-() const { return from_raw(-value_); }

	/// @brief Get the internal value
	[[gnu::always_inline]] [[nodiscard]] constexpr BaseType raw() const noexcept { return value_; }

	/// @brief Construct from a raw value
	[[gnu::always_inline]] static constexpr FixedPoint from_raw(BaseType raw) noexcept {
		FixedPoint result{};
		result.value_ = raw;
		return result;
	}

	/// @brief Addition operator
	/// Add two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator+(const FixedPoint& rhs) const {
		return from_raw(add_saturate(value_, rhs.value_));
	}

	/// @brief Addition operator
	/// Add two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator+=(const FixedPoint& rhs) {
		value_ = add_saturate(value_, rhs.value_);
		return *this;
	}

	/// @brief Subtraction operator
	/// Subtract two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator-(const FixedPoint& rhs) const {
		return from_raw(subtract_saturate(value_, rhs.value_));
	}

	/// @brief Subtraction operator
	/// Subtract two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator-=(const FixedPoint& rhs) {
		value_ = subtract_saturate(value_, rhs.value_);
		return *this;
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator*(const FixedPoint& rhs) const {
		if constexpr (fast_approximation && fractional_bits > 16) {
			// less than 16 would mean no fractional bits remain after right shift by 32
			constexpr int32_t shift = fractional_bits - ((fractional_bits * 2) - 32);
			return from_raw(signed_most_significant_word_multiply(value_, rhs.value_) << shift);
		}
		else if constexpr (rounded) {
			IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.value_) >> (fractional_bits - 1);
			return from_raw(static_cast<BaseType>((value >> 1) + (value % 2)));
		}
		else {
			IntermediateType value = (static_cast<IntermediateType>(value_) * rhs.value_) >> fractional_bits;
			return from_raw(static_cast<BaseType>(value));
		}
	}

	/// @brief Multiplication operator
	/// Multiply two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator*=(const FixedPoint& rhs) {
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
	[[gnu::always_inline]] constexpr FixedPoint operator*=(const FixedPoint<OtherFractionalBits>& rhs) {
		value_ = this->operator*(rhs).value_;
		return *this;
	}

	/// @brief Division operator
	/// Divide two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator/(const FixedPoint& rhs) const {
		if constexpr (rounded) {
			IntermediateType value = (static_cast<IntermediateType>(value_) << (fractional_bits + 1)) / rhs.value_;
			return from_raw(static_cast<BaseType>((value / 2) + (value % 2)));
		}
		else {
			IntermediateType value = (static_cast<IntermediateType>(value_) << fractional_bits) / rhs.value_;
			return from_raw(static_cast<BaseType>(value));
		}
	}

	/// @brief Division operator
	/// Divide two fixed point numbers with different number of fractional bits
	template <std::size_t OtherFractionalBits,
	          std::size_t ResultFractionalBits = std::max(FractionalBits, OtherFractionalBits)
	                                             - std::min(FractionalBits, OtherFractionalBits)>

	requires(ResultFractionalBits < std::max(FractionalBits, OtherFractionalBits))
	        && (ResultFractionalBits > std::min(FractionalBits, OtherFractionalBits))
	constexpr FixedPoint<ResultFractionalBits, Rounded, FastApproximation>
	operator/(const FixedPoint<OtherFractionalBits>& rhs) const {
		if (rhs.raw() == 0) {
			return FixedPoint<ResultFractionalBits>::from_raw(std::numeric_limits<BaseType>::max());
		}

		constexpr int shift = static_cast<int32_t>(FractionalBits) - OtherFractionalBits;
		IntermediateType result{};
		if constexpr (shift > 0) {
			// this means we have more fractional bits than the divisor
			result = (static_cast<IntermediateType>(value_) << shift) / rhs.raw();
		}
		else if constexpr (shift < 0) {
			// this means we have less fractional bits than the divisor
			result = (static_cast<IntermediateType>(value_) / (rhs.raw() << -shift));
		}
		else {
			// same number of fractional bits
			result = static_cast<IntermediateType>(value_) / rhs.raw();
		}
		size_t result_shift = std::max(FractionalBits, OtherFractionalBits) - ResultFractionalBits;

		if constexpr (rounded) {
			// round the result
			result += (1 << (result_shift - 1)); // add half to round
		}
		// shift to get the correct number of fractional bits
		return FixedPoint<ResultFractionalBits, Rounded, FastApproximation>::from_raw(result >> result_shift);
	}

	/// @brief Division operator
	/// Divide two fixed point numbers with the same number of fractional bits
	[[gnu::always_inline]] constexpr FixedPoint operator/=(const FixedPoint& rhs) {
		value_ = this->operator/(rhs).value_;
		return *this;
	}

	/// a + b * c
	/// @brief Fused multiply-add operation for fixed point numbers with a different number of fractional bits
	template <std::size_t OtherFractionalBitsA, std::size_t OtherFractionalBitsB>
	constexpr FixedPoint MultiplyAdd(const FixedPoint<OtherFractionalBitsA>& a,
	                                 const FixedPoint<OtherFractionalBitsB>& b) const {
		// ensure that the number of fractional bits in the addend is equal to the sum of the number of fractional bits
		// in multiplicands, minus 32 (due to right shift) before using smmla/smmlar
		if constexpr (fast_approximation && (OtherFractionalBitsA + OtherFractionalBitsB) - 32 == FractionalBits) {
			return from_raw(signed_most_significant_word_multiply_add(value_, a.raw(), b.raw()));
		}
		else {
			return *this + static_cast<FixedPoint>(a * b);
		}
	}

	/// a + b * c
	/// @brief Fused multiply-add operation for fixed point numbers with the same number of fractional bits
	[[nodiscard]] constexpr FixedPoint MultiplyAdd(const FixedPoint& a, const FixedPoint& b) const {
		if constexpr (fast_approximation && (((fractional_bits * 2) - 32) == (fractional_bits - 1))) {
			// fractional_bits - 1 is due to the left shift
			return from_raw(signed_most_significant_word_multiply_add(value_, a.raw(), b.raw()) << 1);
		}
		else {
			return *this + (a * b);
		}
	}

	/// @brief Equality operator
	[[gnu::always_inline]] constexpr bool operator==(const FixedPoint& rhs) const noexcept {
		return value_ == rhs.value_;
	}

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
		if constexpr (fractional_bits > OtherFractionalBits) {
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
	requires std::floating_point<T>
	[[gnu::always_inline]] constexpr bool operator==(const T& rhs) const noexcept {
		return value_ == FixedPoint{rhs}.value_;
	}

	/// @brief Multiply by an integral type
	template <std::integral T>
	[[gnu::always_inline]] constexpr FixedPoint MultiplyInt(const T& rhs) const {
		return FixedPoint::from_raw(value_ * static_cast<BaseType>(rhs));
	}

	/// @brief Divide by an integral type
	template <std::integral T>
	[[gnu::always_inline]] constexpr FixedPoint DivideInt(const T& rhs) const {
		return FixedPoint::from_raw(value_ / static_cast<BaseType>(rhs));
	}

	template <std::integral T>
	[[gnu::always_inline]] constexpr FixedPoint operator*(const T& rhs) const {
		return MultiplyInt(rhs);
	}

	template <std::integral T>
	[[gnu::always_inline]] constexpr FixedPoint operator/(const T& rhs) {
		return DivideInt(rhs);
	}

	[[gnu::always_inline]] [[nodiscard]] constexpr int32_t integral() const {
		return static_cast<int32_t>(value_ >> fractional_bits);
	}

	[[gnu::always_inline]] constexpr FixedPoint absolute() const noexcept {
		if constexpr (fractional_bits == 31) {
			// This converts the range -2^31 to 2^31 to the range 0-2^31
			return from_raw((value_ / 2) + (1073741824));
		}
		else {
			return from_raw(value_ < 0 ? -value_ : value_);
		}
	}

private:
	BaseType value_{0};
};

/// @brief Multiply by an integral type
template <std::integral T, std::size_t FractionalBits, bool Rounded, bool FastApproximation>
constexpr FixedPoint<FractionalBits, Rounded, FastApproximation>
operator*(const T& lhs, const FixedPoint<FractionalBits, Rounded, FastApproximation>& rhs) {
	return rhs.MultiplyInt(lhs);
}

template <std::floating_point T, std::size_t FractionalBits, bool Rounded, bool FastApproximation>
constexpr FixedPoint<FractionalBits, Rounded, FastApproximation>
operator+(const T& lhs, const FixedPoint<FractionalBits, Rounded, FastApproximation>& rhs) {
	return rhs + lhs;
}

template <std::floating_point T, std::size_t FractionalBits, bool Rounded, bool FastApproximation>
constexpr FixedPoint<FractionalBits, Rounded, FastApproximation>
operator-(const T& lhs, const FixedPoint<FractionalBits, Rounded, FastApproximation>& rhs) {
	return FixedPoint<FractionalBits, Rounded, FastApproximation>{lhs} - rhs;
}

template <std::floating_point T, std::size_t FractionalBits, bool Rounded, bool FastApproximation>
constexpr FixedPoint<FractionalBits, Rounded, FastApproximation>
operator*(const T& lhs, const FixedPoint<FractionalBits, Rounded, FastApproximation>& rhs) {
	return rhs * lhs;
}

constexpr int32_t ONE_Q31 = FixedPoint<31>{1.f}.raw();
constexpr float ONE_Q31f = static_cast<float>(ONE_Q31);
constexpr int32_t ONE_Q15 = FixedPoint<15>{1.f}.raw();
constexpr int32_t NEGATIVE_ONE_Q31 = FixedPoint<31>{-1.f}.raw();
constexpr int32_t ONE_OVER_SQRT2_Q31 = FixedPoint<31>{1 / std::numbers::sqrt2}.raw();
