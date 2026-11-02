/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/lane_pack.h"

#include <cstddef>

/// @file
/// @brief fixed_point.h's operations on lane packs: each applies its scalar operation lane by lane.
///
/// A kernel's step written once over its value type V runs on a `Q<F>` for one stream and on `LanePack<Q<F>, N>` for N
/// streams. The lanes are scalar, so `mul` on two lanes is two `smmul`: independent instructions the A9 overlaps, not
/// NEON. A scalar operand broadcasts, as a coefficient every lane shares does. The result's format is the scalar
/// operation's. A pack of `Fixed` has no arithmetic operators, as a `Fixed` has none.
namespace deluge::dsp {

namespace detail {

template <class T>
inline constexpr bool kIsFixed = false;
template <class S, int F>
inline constexpr bool kIsFixed<Fixed<S, F>> = true;

template <class T>
inline constexpr bool kIsFixedPack = false;
template <class T, std::size_t N>
inline constexpr bool kIsFixedPack<LanePack<T, N>> = kIsFixed<T>;

template <class T>
inline constexpr std::size_t kPackWidth = 0;
template <class T, std::size_t N>
inline constexpr std::size_t kPackWidth<LanePack<T, N>> = N;

// Every width is 0 (a scalar) or the one pack width, so OR-ing them gives that width; lanewise_operands checks it.
template <class... Xs>
inline constexpr std::size_t kWidest = (std::size_t{0} | ... | kPackWidth<Xs>);

/// The operands of one lane-wise operation: each a fixed-point pack or scalar, at least one a pack, every pack one
/// width.
template <class... Xs>
concept lanewise_operands = ((kIsFixed<Xs> || kIsFixedPack<Xs>) && ...) && (kIsFixedPack<Xs> || ...)
                            && ((kPackWidth<Xs> == 0 || kPackWidth<Xs> == kWidest<Xs...>) && ...);

/// @brief Lane @p i of a pack, or a scalar itself.
template <class X>
[[gnu::always_inline, nodiscard]] constexpr auto lane_at(const X& x, std::size_t i) noexcept {
	if constexpr (kIsFixedPack<X>) {
		return x.lane[i];
	}
	else {
		return x;
	}
}

/// @brief @p op applied to lane i of every operand, for each lane in order.
template <class Op, class... Xs>
[[gnu::always_inline, nodiscard]] constexpr auto lanewise(Op op, const Xs&... xs) noexcept {
	constexpr std::size_t kN = kWidest<Xs...>;
	using R = decltype(op(lane_at(xs, 0)...));
	LanePack<R, kN> r;
	for (std::size_t i = 0; i < kN; ++i) {
		r.lane[i] = op(lane_at(xs, i)...);
	}
	return r;
}

} // namespace detail

// Sums, differences and clamps.

/// @brief add_wrap in each lane (`add` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto add_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_wrap(x, y); }, a, b);
}

/// @brief sub_wrap in each lane (`sub` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto sub_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_wrap(x, y); }, a, b);
}

/// @brief add_sat in each lane (`qadd` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto add_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_sat(x, y); }, a, b);
}

/// @brief sub_sat in each lane (`qsub` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto sub_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_sat(x, y); }, a, b);
}

/// @brief add_double_sat in each lane (`qdadd` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto add_double_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_double_sat(x, y); }, a, b);
}

/// @brief sub_double_sat in each lane (`qdsub` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto sub_double_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_double_sat(x, y); }, a, b);
}

/// @brief add_halves_sat in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto add_halves_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_halves_sat(x, y); }, a, b);
}

/// @brief clamp_high in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto clamp_high(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return clamp_high(x, y); }, a, b);
}

/// @brief neg_wrap in each lane (`rsb` per lane).
template <class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto neg_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return neg_wrap(x); }, a);
}

/// @brief neg_sat in each lane (`qsub` from zero per lane).
template <class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto neg_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return neg_sat(x); }, a);
}

/// @brief halve_floor in each lane.
template <class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto halve_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return halve_floor(x); }, a);
}

/// @brief halve_toward_zero in each lane.
template <class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto halve_toward_zero(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return halve_toward_zero(x); }, a);
}

/// @brief clamp_bits<N> in each lane (`ssat` per lane).
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto clamp_bits(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return clamp_bits<N>(x); }, a);
}

/// @brief clamp_unsigned_bits<N> in each lane (`usat` per lane).
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto clamp_unsigned_bits(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return clamp_unsigned_bits<N>(x); }, a);
}

// Multiplies: the result format is the scalar multiply's.

/// @brief mul in each lane (`smmul` per lane for two Q operands).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul(x, y); }, a, b);
}

/// @brief mul_r in each lane (`smmulr` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_r(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_r(x, y); }, a, b);
}

/// @brief mul_wide in each lane (`smull` per lane).
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_wide(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_wide(x, y); }, a, b);
}

/// @brief mul_doubling_sat in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_doubling_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_doubling_sat(x, y); }, a, b);
}

/// @brief mul_doubling_r_sat in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_doubling_r_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_doubling_r_sat(x, y); }, a, b);
}

/// @brief mla in each lane (`smmla` per lane for Q operands).
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mla(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla(s, x, y); }, acc, a, b);
}

/// @brief mla_r in each lane (`smmlar` per lane).
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mla_r(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla_r(s, x, y); }, acc, a, b);
}

/// @brief mls in each lane (`smmls` per lane).
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mls(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mls(s, x, y); }, acc, a, b);
}

/// @brief mls_r in each lane (`smmlsr` per lane).
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mls_r(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mls_r(s, x, y); }, acc, a, b);
}

/// @brief mla_wide in each lane (`smlal` per lane).
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mla_wide(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla_wide(s, x, y); }, acc, a,
	                        b);
}

// Reformats: the value kept, the format changed.

/// @brief as_wrap<To> in each lane.
template <class To, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto as_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_wrap<To>(x); }, a);
}

/// @brief as_sat<To> in each lane.
template <class To, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto as_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_sat<To>(x); }, a);
}

/// @brief as_clamp_then_shift<To> in each lane.
template <class To, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto as_clamp_then_shift(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_clamp_then_shift<To>(x); }, a);
}

/// @brief as_floor<To> in each lane.
template <class To, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto as_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_floor<To>(x); }, a);
}

/// @brief as_round<To> in each lane.
template <class To, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto as_round(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_round<To>(x); }, a);
}

/// @brief mul_as_wrap<To> in each lane.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_as_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_as_wrap<To>(x, y); }, a, b);
}

/// @brief mul_as_sat<To> in each lane.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_as_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_as_sat<To>(x, y); }, a, b);
}

/// @brief mul_r_as_wrap<To> in each lane.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto mul_r_as_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_r_as_wrap<To>(x, y); }, a, b);
}

// Scales: the format kept, the value scaled by 2^N.

/// @brief scale_shl_wrap<N> in each lane.
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_wrap<N>(x); }, a);
}

/// @brief scale_shl_sat<N> in each lane.
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_sat<N>(x); }, a);
}

/// @brief scale_shl_clamp_then_shift<N> in each lane.
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_clamp_then_shift(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_clamp_then_shift<N>(x); }, a);
}

/// @brief scale_shr_floor<N> in each lane.
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto scale_shr_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shr_floor<N>(x); }, a);
}

/// @brief scale_shr_round<N> in each lane.
template <int N, class A>
requires detail::lanewise_operands<A>
[[gnu::always_inline, nodiscard]] constexpr auto scale_shr_round(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shr_round<N>(x); }, a);
}

// Comparisons: a pack of bool, which select() reads.

/// @brief Whether a < b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator<(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x < y; }, a, b);
}

/// @brief Whether a > b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator>(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x > y; }, a, b);
}

/// @brief Whether a <= b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator<=(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x <= y; }, a, b);
}

/// @brief Whether a >= b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator>=(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x >= y; }, a, b);
}

/// @brief Whether a == b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator==(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x == y; }, a, b);
}

/// @brief Whether a != b, in each lane.
template <class A, class B>
requires detail::lanewise_operands<A, B>
[[gnu::always_inline, nodiscard]] constexpr auto operator!=(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return x != y; }, a, b);
}

} // namespace deluge::dsp
