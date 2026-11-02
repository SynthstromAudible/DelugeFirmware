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

#include <algorithm>
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

namespace detail {

template <class S>
[[gnu::always_inline, nodiscard]] constexpr S wrap_add(S a, S b) noexcept {
	using U = std::make_unsigned_t<S>;
	return static_cast<S>(static_cast<U>(static_cast<U>(a) + static_cast<U>(b)));
}
template <class S>
[[gnu::always_inline, nodiscard]] constexpr S wrap_sub(S a, S b) noexcept {
	using U = std::make_unsigned_t<S>;
	return static_cast<S>(static_cast<U>(static_cast<U>(a) - static_cast<U>(b)));
}
template <class S>
[[gnu::always_inline, nodiscard]] constexpr S wrap_neg(S a) noexcept {
	return wrap_sub(S{0}, a);
}
template <class S>
[[gnu::always_inline, nodiscard]] constexpr S wrap_shl(S a, int n) noexcept {
	using U = std::make_unsigned_t<S>;
	return static_cast<S>(static_cast<U>(static_cast<U>(a) << n));
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t sat32(std::int64_t v) noexcept {
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, INT32_MIN, INT32_MAX));
}

/// The exact semantics of each ACLE path, for hosts and for constant evaluation.
namespace portable {

[[gnu::always_inline, nodiscard]] constexpr std::int32_t qadd(std::int32_t a, std::int32_t b) noexcept {
	return sat32(std::int64_t{a} + b);
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t qsub(std::int32_t a, std::int32_t b) noexcept {
	return sat32(std::int64_t{a} - b);
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t qdadd(std::int32_t a, std::int32_t b) noexcept {
	return qadd(a, qadd(b, b));
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t qdsub(std::int32_t a, std::int32_t b) noexcept {
	return qsub(a, qadd(b, b));
}
template <int N>
[[gnu::always_inline, nodiscard]] constexpr std::int32_t ssat(std::int32_t v) noexcept {
	static_assert(N >= 1 && N <= 32);
	constexpr std::int64_t kHi = (std::int64_t{1} << (N - 1)) - 1;
	constexpr std::int64_t kLo = -kHi - 1;
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, kLo, kHi));
}
template <int N>
[[gnu::always_inline, nodiscard]] constexpr std::int32_t usat(std::int32_t v) noexcept {
	static_assert(N >= 0 && N <= 31);
	constexpr auto kHi = static_cast<std::int32_t>((std::uint32_t{1} << N) - 1);
	return std::clamp(v, 0, kHi);
}
[[gnu::always_inline, nodiscard]] constexpr std::int16_t sat16(std::int32_t v) noexcept {
	return static_cast<std::int16_t>(std::clamp<std::int32_t>(v, INT16_MIN, INT16_MAX));
}
/// Applies @p op to each int16 lane of two packed words.
template <class Op>
[[gnu::always_inline, nodiscard]] constexpr std::uint32_t lanes16(std::uint32_t a, std::uint32_t b, Op op) noexcept {
	const auto lo =
	    static_cast<std::uint16_t>(op(static_cast<std::int16_t>(a & 0xFFFFu), static_cast<std::int16_t>(b & 0xFFFFu)));
	const auto hi =
	    static_cast<std::uint16_t>(op(static_cast<std::int16_t>(a >> 16), static_cast<std::int16_t>(b >> 16)));
	return lo | (std::uint32_t{hi} << 16);
}

} // namespace portable

} // namespace detail

/// @brief a + b, wrapping modulo 2^bits (`add`). Defined behaviour: computed in the unsigned storage.
template <class S, int F>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> add_wrap(Fixed<S, F> a, Fixed<S, F> b) noexcept {
	return Fixed<S, F>::from_raw(detail::wrap_add(a.raw(), b.raw()));
}
/// @brief a − b, wrapping modulo 2^bits (`sub`).
template <class S, int F>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> sub_wrap(Fixed<S, F> a, Fixed<S, F> b) noexcept {
	return Fixed<S, F>::from_raw(detail::wrap_sub(a.raw(), b.raw()));
}
/// @brief −a, wrapping: −min() is min() (`rsb`).
template <class S, int F>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> neg_wrap(Fixed<S, F> a) noexcept {
	return Fixed<S, F>::from_raw(detail::wrap_neg(a.raw()));
}

/// @brief a + b, saturating to [min, max] (`qadd`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> add_sat(Q<F> a, Q<F> b) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::qadd(a.raw(), b.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(__qadd(a.raw(), b.raw()));
#else
	return Q<F>::from_raw(detail::portable::qadd(a.raw(), b.raw()));
#endif
}
/// @brief a − b, saturating to [min, max] (`qsub`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> sub_sat(Q<F> a, Q<F> b) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::qsub(a.raw(), b.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(__qsub(a.raw(), b.raw()));
#else
	return Q<F>::from_raw(detail::portable::qsub(a.raw(), b.raw()));
#endif
}
/// @brief −a, saturating: −min() is max() (`qsub` from zero).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> neg_sat(Q<F> a) noexcept {
	return sub_sat(Q<F>::from_raw(0), a);
}
/// @brief sat(a + sat(2b)): adds b doubled, each step saturating (`qdadd`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> add_double_sat(Q<F> a, Q<F> b) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::qdadd(a.raw(), b.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(__qadd(a.raw(), __qdbl(b.raw())));
#else
	return Q<F>::from_raw(detail::portable::qdadd(a.raw(), b.raw()));
#endif
}
/// @brief sat(a − sat(2b)): subtracts b doubled, each step saturating (`qdsub`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> sub_double_sat(Q<F> a, Q<F> b) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::qdsub(a.raw(), b.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(__qsub(a.raw(), __qdbl(b.raw())));
#else
	return Q<F>::from_raw(detail::portable::qdsub(a.raw(), b.raw()));
#endif
}

/// @brief Clamps to the signed N-bit range [−2^(N−1), 2^(N−1) − 1], keeping the format (`ssat #N`).
template <int N, int F>
requires(N >= 1 && N <= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<F> clamp_bits(Q<F> x) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::ssat<N>(x.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(__ssat(x.raw(), N));
#else
	return Q<F>::from_raw(detail::portable::ssat<N>(x.raw()));
#endif
}
/// @brief Clamps to the signed @p n-bit range, @p n known only at run time (1 to 32; two compares, no single
/// instruction).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> clamp_bits(Q<F> x, int n) noexcept {
	const std::int64_t hi = (std::int64_t{1} << (n - 1)) - 1;
	const std::int64_t lo = -hi - 1;
	const std::int64_t v = x.raw();
	return Q<F>::from_raw(static_cast<std::int32_t>(std::clamp(v, lo, hi)));
}
/// @brief Clamps to [0, 2^N − 1], keeping the format (`usat #N`).
template <int N, int F>
requires(N >= 0 && N <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<F> clamp_unsigned_bits(Q<F> x) noexcept {
	if consteval {
		return Q<F>::from_raw(detail::portable::usat<N>(x.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<F>::from_raw(static_cast<std::int32_t>(__usat(x.raw(), N)));
#else
	return Q<F>::from_raw(detail::portable::usat<N>(x.raw()));
#endif
}
/// @brief The smaller of @p x and @p ceiling: an upper clamp only.
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> clamp_high(Q<F> x, Q<F> ceiling) noexcept {
	return x < ceiling ? x : ceiling;
}

/// @brief sat((a >> 1) + (b >> 1)) << 1: the operands' halves summed, then saturate-doubled (`asr`, `add`,
/// `ssat #31`, `lsl #1`). Its ceiling is 2^31 − 2, as lshift_and_saturate<1> gives.
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> add_halves_sat(Q<F> a, Q<F> b) noexcept {
	const auto halves = Q<F>::from_raw(detail::wrap_add(a.raw() >> 1, b.raw() >> 1));
	return Q<F>::from_raw(detail::wrap_shl(clamp_bits<31>(halves).raw(), 1));
}

/// @brief Lane-wise a + b, each lane saturating to the int16 range (`qadd16`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q16x2<F> add_sat(Q16x2<F> a, Q16x2<F> b) noexcept {
	if consteval {
		return Q16x2<F>::from_raw(detail::portable::lanes16(
		    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return detail::portable::sat16(x + y); }));
	}
#ifdef __ARM_FEATURE_DSP
	return Q16x2<F>::from_raw(
	    static_cast<std::uint32_t>(__qadd16(static_cast<int16x2_t>(a.raw()), static_cast<int16x2_t>(b.raw()))));
#else
	return Q16x2<F>::from_raw(detail::portable::lanes16(
	    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return detail::portable::sat16(x + y); }));
#endif
}
/// @brief Lane-wise a − b, each lane saturating to the int16 range (`qsub16`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q16x2<F> sub_sat(Q16x2<F> a, Q16x2<F> b) noexcept {
	if consteval {
		return Q16x2<F>::from_raw(detail::portable::lanes16(
		    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return detail::portable::sat16(x - y); }));
	}
#ifdef __ARM_FEATURE_DSP
	return Q16x2<F>::from_raw(
	    static_cast<std::uint32_t>(__qsub16(static_cast<int16x2_t>(a.raw()), static_cast<int16x2_t>(b.raw()))));
#else
	return Q16x2<F>::from_raw(detail::portable::lanes16(
	    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return detail::portable::sat16(x - y); }));
#endif
}
/// @brief Lane-wise (a + b) >> 1, never overflowing (`shadd16`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q16x2<F> halving_add(Q16x2<F> a, Q16x2<F> b) noexcept {
	if consteval {
		return Q16x2<F>::from_raw(detail::portable::lanes16(
		    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return static_cast<std::int16_t>((x + y) >> 1); }));
	}
#ifdef __ARM_FEATURE_DSP
	return Q16x2<F>::from_raw(
	    static_cast<std::uint32_t>(__shadd16(static_cast<int16x2_t>(a.raw()), static_cast<int16x2_t>(b.raw()))));
#else
	return Q16x2<F>::from_raw(detail::portable::lanes16(
	    a.raw(), b.raw(), [](std::int16_t x, std::int16_t y) { return static_cast<std::int16_t>((x + y) >> 1); }));
#endif
}

} // namespace deluge::dsp
