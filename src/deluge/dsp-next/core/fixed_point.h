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

namespace detail {

[[gnu::always_inline, nodiscard]] constexpr std::int32_t high_word(std::int32_t a, std::int32_t b) noexcept {
	return static_cast<std::int32_t>((std::int64_t{a} * b) >> 32);
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t high_word_r(std::int32_t a, std::int32_t b) noexcept {
	return static_cast<std::int32_t>((std::int64_t{a} * b + 0x80000000LL) >> 32);
}
/// (a × b16) >> 16, b16 the low half of @p b sign-extended: smulwb.
[[gnu::always_inline, nodiscard]] constexpr std::int32_t mul_word_half(std::int32_t a, std::int16_t b) noexcept {
	return static_cast<std::int32_t>((std::int64_t{a} * b) >> 16);
}
[[gnu::always_inline, nodiscard]] constexpr std::int32_t lane_product(std::uint32_t a, std::uint32_t b,
                                                                      int shift) noexcept {
	return std::int32_t{static_cast<std::int16_t>(a >> shift)} * static_cast<std::int16_t>(b >> shift);
}

} // namespace detail

/// @brief The high word of a × b, truncated: Q<A> × Q<B> → Q<A+B−32> (`smmul`). Q31 × Q31 gives Q30.
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mul(Q<A> a, Q<B> b) noexcept {
	return Q<A + B - 32>::from_raw(detail::high_word(a.raw(), b.raw()));
}
/// @brief The high word of a × b, rounded half up: Q<A> × Q<B> → Q<A+B−32> (`smmulr`).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mul_r(Q<A> a, Q<B> b) noexcept {
	return Q<A + B - 32>::from_raw(detail::high_word_r(a.raw(), b.raw()));
}
/// @brief acc + the high word of a × b, wrapping (`smmla`).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mla(Q<A + B - 32> acc, Q<A> a, Q<B> b) noexcept {
	return Q<A + B - 32>::from_raw(detail::wrap_add(acc.raw(), detail::high_word(a.raw(), b.raw())));
}
/// @brief acc + the rounded high word of a × b, wrapping (`smmlar`).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mla_r(Q<A + B - 32> acc, Q<A> a, Q<B> b) noexcept {
	return Q<A + B - 32>::from_raw(detail::wrap_add(acc.raw(), detail::high_word_r(a.raw(), b.raw())));
}
/// @brief The high word of acc × 2^32 − a × b, wrapping (`smmls`): acc − ceil(a × b / 2^32), not acc − mul(a, b).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mls(Q<A + B - 32> acc, Q<A> a, Q<B> b) noexcept {
	const auto high = static_cast<std::int32_t>((-(std::int64_t{a.raw()} * b.raw())) >> 32);
	return Q<A + B - 32>::from_raw(detail::wrap_add(acc.raw(), high));
}
/// @brief The rounded high word of acc × 2^32 − a × b, wrapping (`smmlsr`).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mls_r(Q<A + B - 32> acc, Q<A> a, Q<B> b) noexcept {
	const auto high = static_cast<std::int32_t>((0x80000000LL - std::int64_t{a.raw()} * b.raw()) >> 32);
	return Q<A + B - 32>::from_raw(detail::wrap_add(acc.raw(), high));
}
/// @brief The exact product: Q<A> × Q<B> → Q64<A+B> (`smull`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q64<A + B> mul_wide(Q<A> a, Q<B> b) noexcept {
	return Q64<A + B>::from_raw(std::int64_t{a.raw()} * b.raw());
}
/// @brief acc + the exact product, wrapping modulo 2^64 (`smlal`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q64<A + B> mla_wide(Q64<A + B> acc, Q<A> a, Q<B> b) noexcept {
	return Q64<A + B>::from_raw(detail::wrap_add(acc.raw(), std::int64_t{a.raw()} * b.raw()));
}
/// @brief The high word of an unsigned product: UQ<A> × UQ<B> → UQ<A+B−32> (`umull`).
template <int A, int B>
requires(A + B >= 32)
[[gnu::always_inline, nodiscard]] constexpr UQ<A + B - 32> mul(UQ<A> a, UQ<B> b) noexcept {
	return UQ<A + B - 32>::from_raw(static_cast<std::uint32_t>((std::uint64_t{a.raw()} * b.raw()) >> 32));
}
/// @brief The high word of unsigned a × signed b, truncated: UQ<A> × Q<B> → Q<A+B−32>. Exact for every a, unlike
/// smmul on a's bits read as signed.
template <int A, int B>
requires(A + B >= 32 && A + B - 32 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 32> mul(UQ<A> a, Q<B> b) noexcept {
	return Q<A + B - 32>::from_raw(static_cast<std::int32_t>((static_cast<std::int64_t>(a.raw()) * b.raw()) >> 32));
}
/// @brief sat((2 × a × b) >> 32): the doubled high word, as vqdmulh. Q<A> × Q<B> → Q<A+B−31>. Differs from
/// as_sat<Q<31>>(mul(a, b)) in the LSB on about half of all inputs; only min × min saturates.
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 31> mul_doubling_sat(Q<A> a, Q<B> b) noexcept {
	if (a.raw() == INT32_MIN && b.raw() == INT32_MIN) {
		return Q<A + B - 31>::max();
	}
	return Q<A + B - 31>::from_raw(static_cast<std::int32_t>((std::int64_t{a.raw()} * b.raw()) >> 31));
}
/// @brief sat((2 × a × b + 2^31) >> 32): the rounded doubled high word, as vqrdmulh. Q<A> × Q<B> → Q<A+B−31>.
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 31> mul_doubling_r_sat(Q<A> a, Q<B> b) noexcept {
	if (a.raw() == INT32_MIN && b.raw() == INT32_MIN) {
		return Q<A + B - 31>::max();
	}
	return Q<A + B - 31>::from_raw(static_cast<std::int32_t>((std::int64_t{a.raw()} * b.raw() + (1LL << 30)) >> 31));
}

/// @brief (a × b) >> 16 with a 16-bit b: Q<A> × Q16<B> → Q<A+B−16> (`smulwb`).
template <int A, int B>
requires(A + B >= 16 && A + B - 16 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 16> mul(Q<A> a, Q16<B> b) noexcept {
	if consteval {
		return Q<A + B - 16>::from_raw(detail::mul_word_half(a.raw(), b.raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B - 16>::from_raw(__smulwb(a.raw(), static_cast<int16x2_t>(b.raw())));
#else
	return Q<A + B - 16>::from_raw(detail::mul_word_half(a.raw(), b.raw()));
#endif
}
/// @brief acc + (a × b) >> 16 with a 16-bit b, wrapping (`smlawb`).
template <int A, int B>
requires(A + B >= 16 && A + B - 16 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 16> mla(Q<A + B - 16> acc, Q<A> a, Q16<B> b) noexcept {
	if consteval {
		return Q<A + B - 16>::from_raw(detail::wrap_add(acc.raw(), detail::mul_word_half(a.raw(), b.raw())));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B - 16>::from_raw(__smlawb(a.raw(), static_cast<int16x2_t>(b.raw()), acc.raw()));
#else
	return Q<A + B - 16>::from_raw(detail::wrap_add(acc.raw(), detail::mul_word_half(a.raw(), b.raw())));
#endif
}
/// @brief (a × the low lane of b) >> 16: Q<A> × Q16x2<B> → Q<A+B−16> (`smulwb`).
template <int A, int B>
requires(A + B >= 16 && A + B - 16 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 16> mul_bottom(Q<A> a, Q16x2<B> b) noexcept {
	return mul(a, b.lo());
}
/// @brief (a × the high lane of b) >> 16: Q<A> × Q16x2<B> → Q<A+B−16> (`smulwt`).
template <int A, int B>
requires(A + B >= 16 && A + B - 16 <= 31)
[[gnu::always_inline, nodiscard]] constexpr Q<A + B - 16> mul_top(Q<A> a, Q16x2<B> b) noexcept {
	if consteval {
		return Q<A + B - 16>::from_raw(detail::mul_word_half(a.raw(), b.hi().raw()));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B - 16>::from_raw(__smulwt(a.raw(), static_cast<int16x2_t>(b.raw())));
#else
	return Q<A + B - 16>::from_raw(detail::mul_word_half(a.raw(), b.hi().raw()));
#endif
}

/// @brief lo × lo + hi × hi, wrapping: Q16x2<A> · Q16x2<B> → Q<A+B> (`smuad`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q<A + B> dot(Q16x2<A> x, Q16x2<B> y) noexcept {
	if consteval {
		return Q<A + B>::from_raw(
		    detail::wrap_add(detail::lane_product(x.raw(), y.raw(), 0), detail::lane_product(x.raw(), y.raw(), 16)));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B>::from_raw(__smuad(static_cast<int16x2_t>(x.raw()), static_cast<int16x2_t>(y.raw())));
#else
	return Q<A + B>::from_raw(
	    detail::wrap_add(detail::lane_product(x.raw(), y.raw(), 0), detail::lane_product(x.raw(), y.raw(), 16)));
#endif
}
/// @brief lo × lo − hi × hi, wrapping (`smusd`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q<A + B> dot_diff(Q16x2<A> x, Q16x2<B> y) noexcept {
	if consteval {
		return Q<A + B>::from_raw(
		    detail::wrap_sub(detail::lane_product(x.raw(), y.raw(), 0), detail::lane_product(x.raw(), y.raw(), 16)));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B>::from_raw(__smusd(static_cast<int16x2_t>(x.raw()), static_cast<int16x2_t>(y.raw())));
#else
	return Q<A + B>::from_raw(
	    detail::wrap_sub(detail::lane_product(x.raw(), y.raw(), 0), detail::lane_product(x.raw(), y.raw(), 16)));
#endif
}
/// @brief acc + lo × lo + hi × hi, wrapping (`smlad`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q<A + B> dot_acc(Q<A + B> acc, Q16x2<A> x, Q16x2<B> y) noexcept {
	if consteval {
		return add_wrap(acc, dot(x, y));
	}
#ifdef __ARM_FEATURE_DSP
	return Q<A + B>::from_raw(__smlad(static_cast<int16x2_t>(x.raw()), static_cast<int16x2_t>(y.raw()), acc.raw()));
#else
	return add_wrap(acc, dot(x, y));
#endif
}
/// @brief acc + lo × lo + hi × hi in 64 bits (`smlald`).
template <int A, int B>
[[gnu::always_inline, nodiscard]] constexpr Q64<A + B> dot_wide_acc(Q64<A + B> acc, Q16x2<A> x, Q16x2<B> y) noexcept {
	const auto portable = [&] {
		return Q64<A + B>::from_raw(detail::wrap_add(acc.raw(), std::int64_t{detail::lane_product(x.raw(), y.raw(), 0)}
		                                                            + detail::lane_product(x.raw(), y.raw(), 16)));
	};
	if consteval {
		return portable();
	}
#ifdef __ARM_FEATURE_DSP
	return Q64<A + B>::from_raw(__smlald(static_cast<int16x2_t>(x.raw()), static_cast<int16x2_t>(y.raw()), acc.raw()));
#else
	return portable();
#endif
}

/// @brief The same bits read as another type of the same width. Spells out a sign or format reinterpretation the
/// code depends on; every use carries a comment saying why.
template <class To, class S, int F>
requires(sizeof(typename To::storage_type) == sizeof(S))
[[gnu::always_inline, nodiscard]] constexpr To reinterpret_bits(Fixed<S, F> x) noexcept {
	return To::from_raw(static_cast<To::storage_type>(x.raw()));
}

namespace detail {

template <class To, class S, int F>
concept widening = std::is_same_v<typename To::storage_type, S> && (To::kFractionalBits > F);
template <class To, class S, int F>
concept narrowing = std::is_same_v<typename To::storage_type, S> && (To::kFractionalBits < F);
/// A shift amount that leaves at least one bit of @p S.
template <int N, class S>
concept shift_amount = N >= 1 && N < std::numeric_limits<std::make_unsigned_t<S>>::digits;

/// floor(x / 2^n) + bit (n − 1) of x: round half up, without ever forming x + 2^(n−1).
template <class S>
[[gnu::always_inline, nodiscard]] constexpr S shr_round(S x, int n) noexcept {
	return static_cast<S>((x >> n) + ((x >> (n - 1)) & 1));
}
/// x × 2^n saturated to the int32 range (`qadd x, x` for n = 1; compare-and-select otherwise).
template <int N>
[[gnu::always_inline, nodiscard]] constexpr std::int32_t shl_sat(std::int32_t x) noexcept {
	if constexpr (N == 1) {
		if consteval {
			return portable::qadd(x, x);
		}
#ifdef __ARM_FEATURE_DSP
		return __qadd(x, x);
#else
		return portable::qadd(x, x);
#endif
	}
	else {
		constexpr std::int32_t kHi = INT32_MAX >> N;
		constexpr std::int32_t kLo = INT32_MIN >> N;
		if (x > kHi) {
			return INT32_MAX;
		}
		if (x < kLo) {
			return INT32_MIN;
		}
		return wrap_shl(x, N);
	}
}

} // namespace detail

/// @brief Same value, more fractional bits; bits shifted out of the top wrap (`lsl`).
template <class To, class S, int F>
requires detail::widening<To, S, F>
[[gnu::always_inline, nodiscard]] constexpr To as_wrap(Fixed<S, F> x) noexcept {
	return To::from_raw(detail::wrap_shl(x.raw(), To::kFractionalBits - F));
}
/// @brief Same value, more fractional bits, saturating to [min, max] (`qadd x, x` for one bit; compare-and-select
/// otherwise). The ceiling is INT32_MAX.
template <class To, int F>
requires detail::widening<To, std::int32_t, F>
[[gnu::always_inline, nodiscard]] constexpr To as_sat(Q<F> x) noexcept {
	return To::from_raw(detail::shl_sat<To::kFractionalBits - F>(x.raw()));
}
/// @brief Same value, n = M − F more fractional bits: clamps to 32 − n bits, then shifts (`ssat`, `lsl`). The
/// ceiling is 2^31 − 2^n, not INT32_MAX; this is lshift_and_saturate<n>.
template <class To, int F>
requires detail::widening<To, std::int32_t, F>
[[gnu::always_inline, nodiscard]] constexpr To as_clamp_then_shift(Q<F> x) noexcept {
	constexpr int kN = To::kFractionalBits - F;
	return To::from_raw(detail::wrap_shl(clamp_bits<32 - kN>(x).raw(), kN));
}
/// @brief Same value, fewer fractional bits, rounding toward −∞ (`asr`, or `lsr` unsigned).
template <class To, class S, int F>
requires detail::narrowing<To, S, F>
[[gnu::always_inline, nodiscard]] constexpr To as_floor(Fixed<S, F> x) noexcept {
	return To::from_raw(static_cast<S>(x.raw() >> (F - To::kFractionalBits)));
}
/// @brief Same value, fewer fractional bits, rounding half up; never wraps (`asr`, `and`, `add`).
template <class To, class S, int F>
requires detail::narrowing<To, S, F>
[[gnu::always_inline, nodiscard]] constexpr To as_round(Fixed<S, F> x) noexcept {
	return To::from_raw(detail::shr_round(x.raw(), F - To::kFractionalBits));
}

/// @brief as_wrap<To>(mul(a, b)): the high word reformatted, wrapping (`smmul`, `lsl`). q31_mult is
/// mul_as_wrap<Q<31>>, and −1 × −1 gives −1.
template <class To, int A, int B>
requires(A + B >= 32) && detail::widening<To, std::int32_t, A + B - 32>
[[gnu::always_inline, nodiscard]] constexpr To mul_as_wrap(Q<A> a, Q<B> b) noexcept {
	return as_wrap<To>(mul(a, b));
}
/// @brief as_sat<To>(mul(a, b)): the high word reformatted, saturating (`smmul`, then `qadd` or compares).
template <class To, int A, int B>
requires(A + B >= 32) && detail::widening<To, std::int32_t, A + B - 32>
[[gnu::always_inline, nodiscard]] constexpr To mul_as_sat(Q<A> a, Q<B> b) noexcept {
	return as_sat<To>(mul(a, b));
}
/// @brief as_wrap<To>(mul_r(a, b)): the rounded high word reformatted, wrapping (`smmulr`, `lsl`) — the filters'
/// commonest step.
template <class To, int A, int B>
requires(A + B >= 32) && detail::widening<To, std::int32_t, A + B - 32>
[[gnu::always_inline, nodiscard]] constexpr To mul_r_as_wrap(Q<A> a, Q<B> b) noexcept {
	return as_wrap<To>(mul_r(a, b));
}

/// @brief x × 2^N in the same format, wrapping (`lsl`).
template <int N, class S, int F>
requires detail::shift_amount<N, S>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> scale_shl_wrap(Fixed<S, F> x) noexcept {
	return Fixed<S, F>::from_raw(detail::wrap_shl(x.raw(), N));
}
/// @brief x × 2^N in the same format, saturating to [min, max] (`qadd x, x` for N = 1).
template <int N, int F>
requires detail::shift_amount<N, std::int32_t>
[[gnu::always_inline, nodiscard]] constexpr Q<F> scale_shl_sat(Q<F> x) noexcept {
	return Q<F>::from_raw(detail::shl_sat<N>(x.raw()));
}
/// @brief x clamped to 32 − N bits, then × 2^N, in the same format (`ssat`, `lsl`): lshift_and_saturate<N>.
template <int N, int F>
requires detail::shift_amount<N, std::int32_t>
[[gnu::always_inline, nodiscard]] constexpr Q<F> scale_shl_clamp_then_shift(Q<F> x) noexcept {
	return Q<F>::from_raw(detail::wrap_shl(clamp_bits<32 - N>(x).raw(), N));
}
/// @brief x / 2^N in the same format, rounding toward −∞ (`asr`).
template <int N, class S, int F>
requires detail::shift_amount<N, S>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> scale_shr_floor(Fixed<S, F> x) noexcept {
	return Fixed<S, F>::from_raw(static_cast<S>(x.raw() >> N));
}
/// @brief x / 2^N in the same format, rounding half up; never wraps.
template <int N, class S, int F>
requires detail::shift_amount<N, S>
[[gnu::always_inline, nodiscard]] constexpr Fixed<S, F> scale_shr_round(Fixed<S, F> x) noexcept {
	return Fixed<S, F>::from_raw(detail::shr_round(x.raw(), N));
}
/// @brief x / 2 rounding toward −∞ (`asr #1`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> halve_floor(Q<F> x) noexcept {
	return Q<F>::from_raw(x.raw() >> 1);
}
/// @brief x / 2 rounding toward zero (`add` of the sign bit, `asr #1`).
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F> halve_toward_zero(Q<F> x) noexcept {
	return Q<F>::from_raw(x.raw() / 2);
}

/// @brief The same value in 64 bits with M ≥ F fractional bits; exact.
template <class To, int F>
requires std::is_same_v<typename To::storage_type, std::int64_t> && (To::kFractionalBits >= F)
[[gnu::always_inline, nodiscard]] constexpr To widen(Q<F> x) noexcept {
	return To::from_raw(std::int64_t{x.raw()} << (To::kFractionalBits - F));
}
/// @brief A 64-bit value in 32 bits with M ≤ F fractional bits, rounding toward −∞; the high bits wrap.
template <class To, int F>
requires std::is_same_v<typename To::storage_type, std::int32_t> && (To::kFractionalBits <= F)
[[gnu::always_inline, nodiscard]] constexpr To narrow_floor_wrap(Q64<F> x) noexcept {
	return To::from_raw(static_cast<std::int32_t>(static_cast<std::uint64_t>(x.raw() >> (F - To::kFractionalBits))));
}
/// @brief A 64-bit value in 32 bits with M < F fractional bits, rounding half up; the high bits wrap.
template <class To, int F>
requires std::is_same_v<typename To::storage_type, std::int32_t> && (To::kFractionalBits < F)
[[gnu::always_inline, nodiscard]] constexpr To narrow_round_wrap(Q64<F> x) noexcept {
	return To::from_raw(
	    static_cast<std::int32_t>(static_cast<std::uint64_t>(detail::shr_round(x.raw(), F - To::kFractionalBits))));
}
/// @brief A 64-bit value in 32 bits with M ≤ F fractional bits, rounding toward −∞ and saturating.
template <class To, int F>
requires std::is_same_v<typename To::storage_type, std::int32_t> && (To::kFractionalBits <= F)
[[gnu::always_inline, nodiscard]] constexpr To narrow_floor_sat(Q64<F> x) noexcept {
	return To::from_raw(detail::sat32(x.raw() >> (F - To::kFractionalBits)));
}
/// @brief The top half of x as a 16-bit value, saturating: Q<F> → Q16<F−16> (`ssat #16, x, asr #16`).
template <int F>
requires(F >= 16)
[[gnu::always_inline, nodiscard]] constexpr Q16<F - 16> to_q16_sat(Q<F> x) noexcept {
	return Q16<F - 16>::from_raw(static_cast<std::int16_t>(clamp_bits<16>(scale_shr_floor<16>(x)).raw()));
}
/// @brief A 16-bit value in the top half of 32 bits: Q16<F> → Q<F+16>; exact.
template <int F>
[[gnu::always_inline, nodiscard]] constexpr Q<F + 16> from_q16(Q16<F> x) noexcept {
	return Q<F + 16>::from_raw(detail::wrap_shl(std::int32_t{x.raw()}, 16));
}

} // namespace deluge::dsp
