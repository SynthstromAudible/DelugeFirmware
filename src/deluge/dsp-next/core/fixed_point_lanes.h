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
#include "dsp-next/core/pack.h"

#include <concepts>
#include <cstddef>
#include <utility>

/// @file
/// @brief fixed_point.h's 32-bit arithmetic on lane packs: each operation applies its scalar operation lane by lane.
///
/// Lifted: the wrapping and saturating sums, differences and negations, the clamps (`clamp_high`, `clamp_bits<N>`,
/// `clamp_unsigned_bits<N>`), the multiplies (`mul`, `mul_r`, `mla`, `mla_r`, `mls`, `mls_r`, `mul_wide`, `mla_wide`,
/// `mul_doubling_sat`, `mul_doubling_r_sat`), the reformats (`as_*`, `mul_as_*`) and the scales (`scale_*`, `halve_*`).
/// Not lifted, as no lane kernel uses them yet: the 64-bit narrows and `widen`, the Q16 and Q16x2 forms (`mul_top`,
/// `mul_bottom`, `dot*`, `to_q16_sat`, `from_q16`), the float conversions, `reinterpret_bits` and the run-time
/// `clamp_bits(x, n)`. The comparisons are Pack's own (pack.h). @ref from_raw and @ref to_raw convert a raw
/// scalar or pack and a typed one, for raw words meeting a lane step: the specs' and the codegen probes' references.
///
/// A kernel's step written once over its value type V runs on a `Q<F>` for one stream and on `Pack<Q<F>, N>` for N
/// streams. The lanes are scalar, so `mul` on two lanes is two `smmul`: independent instructions the A9 overlaps, not
/// NEON. A scalar operand broadcasts, as a coefficient every lane shares does. The result's format is the scalar
/// operation's, and an operation exists for a pack only where its scalar operation exists for the lane types. A pack
/// of `Fixed` has no arithmetic operators, as a `Fixed` has none.
namespace deluge::dsp {

namespace detail {

template <class T>
inline constexpr bool kIsFixed = false;
template <class S, int F>
inline constexpr bool kIsFixed<Fixed<S, F>> = true;

template <class T>
inline constexpr bool kIsFixedPack = false;
template <class T, std::size_t N>
inline constexpr bool kIsFixedPack<Pack<T, N>> = kIsFixed<T>;

template <class T>
inline constexpr std::size_t kPackWidth = 0;
template <class T, std::size_t N>
inline constexpr std::size_t kPackWidth<Pack<T, N>> = N;

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

/// @brief The lane type of a pack, or a scalar's own type.
template <class X>
using lane_t = decltype(lane_at(std::declval<const X&>(), 0));

/// @brief @p op applied to lane i of every operand, for each lane in order.
template <class Op, class... Xs>
[[gnu::always_inline, nodiscard]] constexpr auto lanewise(Op op, const Xs&... xs) noexcept {
	constexpr std::size_t kN = kWidest<Xs...>;
	using R = decltype(op(lane_at(xs, 0)...));
	Pack<R, kN> r;
	for (std::size_t i = 0; i < kN; ++i) {
		r.lane[i] = op(lane_at(xs, i)...);
	}
	return r;
}

} // namespace detail

// Sums, differences and clamps.

/// @brief add_wrap in each lane (`add` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's add_wrap.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { add_wrap(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto add_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_wrap(x, y); }, a, b);
}

/// @brief sub_wrap in each lane (`sub` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's sub_wrap.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { sub_wrap(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto sub_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_wrap(x, y); }, a, b);
}

/// @brief add_sat in each lane (`qadd` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's add_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { add_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto add_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_sat(x, y); }, a, b);
}

/// @brief sub_sat in each lane (`qsub` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's sub_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { sub_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto sub_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_sat(x, y); }, a, b);
}

/// @brief add_double_sat in each lane (`qdadd` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's add_double_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { add_double_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto add_double_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_double_sat(x, y); }, a, b);
}

/// @brief sub_double_sat in each lane (`qdsub` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's sub_double_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { sub_double_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto sub_double_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return sub_double_sat(x, y); }, a, b);
}

/// @brief add_halves_sat in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's add_halves_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { add_halves_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto add_halves_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return add_halves_sat(x, y); }, a, b);
}

/// @brief clamp_high in each lane (`cmp` and a select per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's clamp_high.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { clamp_high(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto clamp_high(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return clamp_high(x, y); }, a, b);
}

/// @brief neg_wrap in each lane (`rsb` per lane).
/// @param a A pack.
/// @return The pack of each lane's neg_wrap.
template <class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { neg_wrap(x); }
[[gnu::always_inline, nodiscard]] constexpr auto neg_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return neg_wrap(x); }, a);
}

/// @brief neg_sat in each lane (`qsub` from zero per lane).
/// @param a A pack.
/// @return The pack of each lane's neg_sat.
template <class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { neg_sat(x); }
[[gnu::always_inline, nodiscard]] constexpr auto neg_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return neg_sat(x); }, a);
}

/// @brief halve_floor in each lane.
/// @param a A pack.
/// @return The pack of each lane's halve_floor.
template <class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { halve_floor(x); }
[[gnu::always_inline, nodiscard]] constexpr auto halve_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return halve_floor(x); }, a);
}

/// @brief halve_toward_zero in each lane.
/// @param a A pack.
/// @return The pack of each lane's halve_toward_zero.
template <class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { halve_toward_zero(x); }
[[gnu::always_inline, nodiscard]] constexpr auto halve_toward_zero(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return halve_toward_zero(x); }, a);
}

/// @brief clamp_bits<N> in each lane (`ssat` per lane).
/// @param a A pack.
/// @return The pack of each lane's clamp_bits<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { clamp_bits<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto clamp_bits(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return clamp_bits<N>(x); }, a);
}

/// @brief clamp_unsigned_bits<N> in each lane (`usat` per lane).
/// @param a A pack.
/// @return The pack of each lane's clamp_unsigned_bits<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { clamp_unsigned_bits<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto clamp_unsigned_bits(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return clamp_unsigned_bits<N>(x); }, a);
}

// Multiplies: the result format is the scalar multiply's.

/// @brief mul in each lane (`smmul` for two Q operands per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul(x, y); }, a, b);
}

/// @brief mul_r in each lane (`smmulr` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_r.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_r(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_r(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_r(x, y); }, a, b);
}

/// @brief mul_wide in each lane (`smull` per lane).
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_wide.
template <class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_wide(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_wide(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_wide(x, y); }, a, b);
}

/// @brief mul_doubling_sat in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_doubling_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B>
         && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_doubling_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_doubling_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_doubling_sat(x, y); }, a, b);
}

/// @brief mul_doubling_r_sat in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_doubling_r_sat.
template <class A, class B>
requires detail::lanewise_operands<A, B>
         && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_doubling_r_sat(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_doubling_r_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_doubling_r_sat(x, y); }, a, b);
}

/// @brief mla in each lane (`smmla` per lane).
/// @param acc The accumulator: a pack or a broadcast scalar.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mla.
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
         && requires(detail::lane_t<Acc> s, detail::lane_t<A> x, detail::lane_t<B> y) { mla(s, x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mla(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla(s, x, y); }, acc, a, b);
}

/// @brief mla_r in each lane (`smmlar` per lane).
/// @param acc The accumulator: a pack or a broadcast scalar.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mla_r.
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
         && requires(detail::lane_t<Acc> s, detail::lane_t<A> x, detail::lane_t<B> y) { mla_r(s, x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mla_r(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla_r(s, x, y); }, acc, a, b);
}

/// @brief mls in each lane (`smmls` per lane).
/// @param acc The accumulator: a pack or a broadcast scalar.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mls.
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
         && requires(detail::lane_t<Acc> s, detail::lane_t<A> x, detail::lane_t<B> y) { mls(s, x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mls(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mls(s, x, y); }, acc, a, b);
}

/// @brief mls_r in each lane (`smmlsr` per lane).
/// @param acc The accumulator: a pack or a broadcast scalar.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mls_r.
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
         && requires(detail::lane_t<Acc> s, detail::lane_t<A> x, detail::lane_t<B> y) { mls_r(s, x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mls_r(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mls_r(s, x, y); }, acc, a, b);
}

/// @brief mla_wide in each lane (`smlal` per lane).
/// @param acc The accumulator: a pack or a broadcast scalar.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mla_wide.
template <class Acc, class A, class B>
requires detail::lanewise_operands<Acc, A, B>
         && requires(detail::lane_t<Acc> s, detail::lane_t<A> x, detail::lane_t<B> y) { mla_wide(s, x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mla_wide(Acc acc, A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto s, auto x, auto y) { return mla_wide(s, x, y); }, acc, a,
	                        b);
}

// Reformats: the value kept, the format changed.

/// @brief as_wrap<To> in each lane.
/// @param a A pack.
/// @return The pack of each lane's as_wrap<To>.
template <class To, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { as_wrap<To>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto as_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_wrap<To>(x); }, a);
}

/// @brief as_sat<To> in each lane.
/// @param a A pack.
/// @return The pack of each lane's as_sat<To>.
template <class To, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { as_sat<To>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto as_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_sat<To>(x); }, a);
}

/// @brief as_clamp_then_shift<To> in each lane.
/// @param a A pack.
/// @return The pack of each lane's as_clamp_then_shift<To>.
template <class To, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { as_clamp_then_shift<To>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto as_clamp_then_shift(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_clamp_then_shift<To>(x); }, a);
}

/// @brief as_floor<To> in each lane.
/// @param a A pack.
/// @return The pack of each lane's as_floor<To>.
template <class To, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { as_floor<To>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto as_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_floor<To>(x); }, a);
}

/// @brief as_round<To> in each lane.
/// @param a A pack.
/// @return The pack of each lane's as_round<To>.
template <class To, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { as_round<To>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto as_round(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return as_round<To>(x); }, a);
}

/// @brief mul_as_wrap<To> in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_as_wrap<To>.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B>
         && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_as_wrap<To>(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_as_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_as_wrap<To>(x, y); }, a, b);
}

/// @brief mul_as_sat<To> in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_as_sat<To>.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B> && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_as_sat<To>(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_as_sat(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_as_sat<To>(x, y); }, a, b);
}

/// @brief mul_r_as_wrap<To> in each lane.
/// @param a A pack or a broadcast scalar.
/// @param b A pack or a broadcast scalar.
/// @return The pack of each lane's mul_r_as_wrap<To>.
template <class To, class A, class B>
requires detail::lanewise_operands<A, B>
         && requires(detail::lane_t<A> x, detail::lane_t<B> y) { mul_r_as_wrap<To>(x, y); }
[[gnu::always_inline, nodiscard]] constexpr auto mul_r_as_wrap(A a, B b) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x, auto y) { return mul_r_as_wrap<To>(x, y); }, a, b);
}

// Scales: the format kept, the value scaled by 2^N.

/// @brief scale_shl_wrap<N> in each lane.
/// @param a A pack.
/// @return The pack of each lane's scale_shl_wrap<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { scale_shl_wrap<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_wrap(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_wrap<N>(x); }, a);
}

/// @brief scale_shl_sat<N> in each lane.
/// @param a A pack.
/// @return The pack of each lane's scale_shl_sat<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { scale_shl_sat<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_sat(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_sat<N>(x); }, a);
}

/// @brief scale_shl_clamp_then_shift<N> in each lane.
/// @param a A pack.
/// @return The pack of each lane's scale_shl_clamp_then_shift<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { scale_shl_clamp_then_shift<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto scale_shl_clamp_then_shift(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shl_clamp_then_shift<N>(x); }, a);
}

/// @brief scale_shr_floor<N> in each lane.
/// @param a A pack.
/// @return The pack of each lane's scale_shr_floor<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { scale_shr_floor<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto scale_shr_floor(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shr_floor<N>(x); }, a);
}

/// @brief scale_shr_round<N> in each lane.
/// @param a A pack.
/// @return The pack of each lane's scale_shr_round<N>.
template <int N, class A>
requires detail::lanewise_operands<A> && requires(detail::lane_t<A> x) { scale_shr_round<N>(x); }
[[gnu::always_inline, nodiscard]] constexpr auto scale_shr_round(A a) noexcept {
	return detail::lanewise([] [[gnu::always_inline]] (auto x) { return scale_shr_round<N>(x); }, a);
}

// Boundaries: raw words, a scalar or a pack of lanes, meeting a typed lane step convert here.

/// @brief @p v's bits read in @p T's format: a raw scalar to a @p T, or a pack of raw lanes to a pack of @p T.
/// @details One call converts either form, so a reference written over a raw scalar and a raw pack converts once.
/// @tparam T The format, a `Fixed`.
/// @param v A raw value of @p T's storage type, or a pack of them.
/// @return @p T, or `Pack<T, N>`, holding @p v's bits.
template <class T, class V>
requires detail::kIsFixed<T>
         && (std::same_as<V, typename T::storage_type>
             || std::same_as<V, Pack<typename T::storage_type, detail::kPackWidth<V>>>)
[[gnu::always_inline, nodiscard]] constexpr auto from_raw(V v) noexcept {
	if constexpr (std::same_as<V, typename T::storage_type>) {
		return T::from_raw(v);
	}
	else {
		Pack<T, detail::kPackWidth<V>> r;
		for (std::size_t i = 0; i < detail::kPackWidth<V>; ++i) {
			r.lane[i] = T::from_raw(v.lane[i]);
		}
		return r;
	}
}

/// @brief @p v's bits as its storage type: a `Fixed` to its raw value, or a pack of them to a pack of raw lanes. The
/// reverse of @ref from_raw.
/// @param v A `Fixed`, or a pack of them.
/// @return The storage type, or `Pack<storage, N>`, holding @p v's bits.
template <class V>
requires detail::kIsFixed<V> || detail::kIsFixedPack<V>
[[gnu::always_inline, nodiscard]] constexpr auto to_raw(V v) noexcept {
	if constexpr (detail::kIsFixed<V>) {
		return v.raw();
	}
	else {
		using S = detail::lane_t<V>::storage_type;
		Pack<S, detail::kPackWidth<V>> r;
		for (std::size_t i = 0; i < detail::kPackWidth<V>; ++i) {
			r.lane[i] = v.lane[i].raw();
		}
		return r;
	}
}

} // namespace deluge::dsp
