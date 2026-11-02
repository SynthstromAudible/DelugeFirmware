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

#include <compare>
#include <cstdint>
#include <limits>
#include <type_traits>

#ifdef __ARM_FEATURE_DSP
#include <arm_acle.h>
#endif

/// @file
/// @brief Fixed-point numbers whose format is part of their type, and the named operations on them.
///
/// `Fixed<S, F>` stores a value as `S` holding value × 2^F. There are no arithmetic operators: every operation names
/// its overflow policy (`_wrap`, `_sat`, `_clamp_then_shift`) and rounding (`_floor`, `_round`, `_r`), and a format
/// change is either a reformat (`as_*`: same value, new format) or a scale (`scale_*`: same format, new value). Each
/// operation compiles to the Cortex-A9 instruction its comment names; tests/unit-arm/codegen checks that.

namespace deluge::dsp {

/// @brief A fixed-point number: storage @p S holds the value × 2^@p F.
/// @tparam S the storage integer.
/// @tparam F fractional bits, 0 to the storage's value bits (31 for int32, 32 for uint32).
template <class S, int F>
class Fixed {
	static_assert(std::is_integral_v<S> && !std::is_same_v<S, bool>);
	static_assert(F >= 0 && F <= std::numeric_limits<S>::digits, "F must fit the storage's value bits");

public:
	using storage_type = S;
	static constexpr int kFractionalBits = F;

	Fixed() = default;

	/// @brief Wraps a raw storage value; the only way in.
	[[gnu::always_inline, nodiscard]] static constexpr Fixed from_raw(S raw) noexcept {
		Fixed value;
		value.raw_ = raw;
		return value;
	}
	/// @brief The raw storage value; the only way out.
	[[gnu::always_inline, nodiscard]] constexpr S raw() const noexcept { return raw_; }

	/// @brief One: 2^F, or max() (one minus one LSB) when 2^F does not fit — Q<31>, UQ<32>, Q16<15>, Q64<63>.
	[[gnu::always_inline, nodiscard]] static constexpr Fixed one() noexcept {
		if constexpr (F == std::numeric_limits<S>::digits) {
			return max();
		}
		else {
			return from_raw(static_cast<S>(S{1} << F));
		}
	}
	[[gnu::always_inline, nodiscard]] static constexpr Fixed min() noexcept {
		return from_raw(std::numeric_limits<S>::min());
	}
	[[gnu::always_inline, nodiscard]] static constexpr Fixed max() noexcept {
		return from_raw(std::numeric_limits<S>::max());
	}

	friend constexpr bool operator==(Fixed, Fixed) noexcept = default;
	friend constexpr auto operator<=>(Fixed, Fixed) noexcept = default;

private:
	S raw_;
};

template <int F>
using Q = Fixed<std::int32_t, F>; ///< signed 32-bit, Q(31-F).F
template <int F>
using UQ = Fixed<std::uint32_t, F>; ///< unsigned 32-bit, UQ(32-F).F; UQ<32> is the oscillator phase
template <int F>
using Q16 = Fixed<std::int16_t, F>; ///< signed 16-bit sample data and tables
template <int F>
using Q64 = Fixed<std::int64_t, F>; ///< signed 64-bit intermediates only
template <int F>
using UQ64 = Fixed<std::uint64_t, F>; ///< unsigned 64-bit intermediates only

/// @brief Two Q16<F> values packed low/high in one word, for the ARMv6 SIMD32 instructions.
template <int F>
class Q16x2 {
	static_assert(F >= 0 && F <= 15);

public:
	using storage_type = std::uint32_t;
	static constexpr int kFractionalBits = F;

	Q16x2() = default;

	[[gnu::always_inline, nodiscard]] static constexpr Q16x2 from_raw(std::uint32_t raw) noexcept {
		Q16x2 value;
		value.raw_ = raw;
		return value;
	}
	[[gnu::always_inline, nodiscard]] constexpr std::uint32_t raw() const noexcept { return raw_; }

	/// @brief Packs @p lo into bits 0–15 and @p hi into bits 16–31 (`pkhbt`).
	[[gnu::always_inline, nodiscard]] static constexpr Q16x2 pack(Q16<F> lo, Q16<F> hi) noexcept {
		return from_raw(static_cast<std::uint16_t>(lo.raw())
		                | (std::uint32_t{static_cast<std::uint16_t>(hi.raw())} << 16));
	}
	[[gnu::always_inline, nodiscard]] constexpr Q16<F> lo() const noexcept {
		return Q16<F>::from_raw(static_cast<std::int16_t>(raw_ & 0xFFFFu));
	}
	[[gnu::always_inline, nodiscard]] constexpr Q16<F> hi() const noexcept {
		return Q16<F>::from_raw(static_cast<std::int16_t>(raw_ >> 16));
	}

	friend constexpr bool operator==(Q16x2, Q16x2) noexcept = default;

private:
	std::uint32_t raw_;
};

namespace detail {

/// Never defined: a constant out of range calls it, and a call to a non-constexpr function fails constant evaluation.
void constant_out_of_range();

/// @brief value × 2^F rounded to nearest (half away from zero); a value out of range fails constant evaluation.
template <class T>
consteval T constant(double value) {
	using S = T::storage_type;
	static_assert(T::kFractionalBits <= 32);
	const double scaled = value * static_cast<double>(std::uint64_t{1} << T::kFractionalBits);
	const double rounded = scaled + (scaled >= 0.0 ? 0.5 : -0.5);
	if (rounded != rounded || rounded <= static_cast<double>(std::numeric_limits<S>::min()) - 1.0
	    || rounded >= static_cast<double>(std::numeric_limits<S>::max()) + 1.0) {
		constant_out_of_range();
	}
	return T::from_raw(static_cast<S>(static_cast<std::int64_t>(rounded)));
}

} // namespace detail

/// @brief A Q<F> constant from a literal: `q<31>(0.5)`. Out of range does not compile; use one() for Q<31>'s one.
template <int F>
[[nodiscard]] consteval Q<F> q(double value) {
	return detail::constant<Q<F>>(value);
}
/// @brief A UQ<F> constant from a literal; out of range does not compile.
template <int F>
[[nodiscard]] consteval UQ<F> uq(double value) {
	return detail::constant<UQ<F>>(value);
}
/// @brief A Q16<F> constant from a literal; out of range does not compile.
template <int F>
[[nodiscard]] consteval Q16<F> q16(double value) {
	return detail::constant<Q16<F>>(value);
}

} // namespace deluge::dsp
