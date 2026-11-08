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

#include "dsp-next/core/pack.h"
#include "dsp-next/core/sample.h"

#include <argon.hpp>

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>

/// @file
/// @brief Pack<f32, 2> and Pack<f32, 4> on a NEON d or q register, and mul_add, fused where the target can.
///
/// Every operation is one Argon operation, which is the scalar operation in each lane: a lane of a pack's result is
/// bit for bit the scalar result under flush-to-zero (fp::FlushToZero), which ARMv7 NEON always applies. The scalar
/// overloads here (mul_add, abs, sum) complete the set so that a kernel step written over V compiles for f32.
namespace deluge::dsp {

/// @brief Whether this target has a fused float multiply-add, which @ref mul_add then uses: AArch64, ARMv7 with VFPv4,
/// x86 with FMA. False on the Cortex-A9.
inline constexpr bool kFusedMulAdd = ARGON_HAS_FUSED_MULTIPLY_ADD;

#if defined(__ARM_FEATURE_FMA) || defined(__FMA__)
static_assert(kFusedMulAdd, "the scalar and the pack must fuse together");
#else
static_assert(!kFusedMulAdd, "the scalar and the pack must fuse together");
#endif

/// @brief @p a + @p b · @p c: one rounding where @ref kFusedMulAdd, two where not.
[[nodiscard]] [[gnu::always_inline]] inline f32 mul_add(f32 a, f32 b, f32 c) {
#pragma clang fp contract(off) reassociate(off)
	if constexpr (kFusedMulAdd) {
		return std::fma(b, c, a);
	}
	else {
		return a + (b * c);
	}
}

/// @brief |@p x|: the scalar form of a pack's abs.
[[nodiscard]] [[gnu::always_inline]] inline f32 abs(f32 x) {
	return std::fabs(x);
}

/// @brief @p x: the scalar form of a pack's lane sum, a one-lane pack's only lane.
[[nodiscard]] [[gnu::always_inline]] inline f32 sum(f32 x) {
	return x;
}

namespace detail {
/// The Argon vector holding N f32 lanes: a q register for 4, a d register for 2.
template <std::size_t N>
using F32Vector = std::conditional_t<N == 4, Argon<f32>, ArgonHalf<f32>>;
} // namespace detail

/// @brief The per-lane result of a comparison of two Pack<f32, N>, which @ref select reads.
template <std::size_t N>
requires(N == 2 || N == 4)
struct Mask {
	detail::F32Vector<N>::argon_bool_type bits; ///< All ones in a lane where the comparison held.
};

/// @brief N f32 lanes on one NEON register.
/// @details min and max are `a > b ? b : a` and `a < b ? b : a`, as the scalar ones, not the hardware's, which differ
/// at equal operands (±0) and NaN.
template <std::size_t N>
requires(N == 2 || N == 4)
struct Pack<f32, N> {
	using Vector = detail::F32Vector<N>; ///< The Argon vector.
	Vector v;                            ///< The lanes.

	Pack() = default;
	/// @brief @p x in every lane, as Pack<T, N>'s converting constructor: a scalar operand broadcasts.
	template <std::same_as<f32> U>
	// A scalar converts implicitly, so that one expression compiles for a scalar and for a pack.
	// NOLINTNEXTLINE(google-explicit-constructor,cppcoreguidelines-explicit-constructor,hicpp-explicit-conversions)
	[[gnu::always_inline]] Pack(U x) : v{x} {}
	/// @brief The lanes of @p x.
	[[gnu::always_inline]] explicit Pack(Vector x) : v{x} {}

	[[nodiscard]] [[gnu::always_inline]] static Pack broadcast(f32 x) { return Pack{x}; }
	/// @brief Lanes 0 … N−1 from @p p[0] … @p p[N−1].
	[[nodiscard]] [[gnu::always_inline]] static Pack load(const f32* p) { return Pack{Vector::Load(p)}; }
	/// @brief Lanes 0 … N−1 to @p p[0] … @p p[N−1].
	[[gnu::always_inline]] void store(f32* p) const { v.StoreTo(p); }
	/// @brief Lane @p i, for set-up and inspection: not for a per-sample loop.
	[[nodiscard]] f32 get(std::size_t i) const {
		std::array<f32, N> l{};
		store(l.data());
		return l[i];
	}
	/// @brief Sets lane @p i to @p x, for set-up: not for a per-sample loop.
	void set(std::size_t i, f32 x) {
		std::array<f32, N> l{};
		store(l.data());
		l[i] = x;
		*this = load(l.data());
	}

	[[nodiscard]] [[gnu::always_inline]] friend Pack operator+(Pack a, Pack b) { return Pack{a.v + b.v}; }
	[[nodiscard]] [[gnu::always_inline]] friend Pack operator-(Pack a, Pack b) { return Pack{a.v - b.v}; }
	[[nodiscard]] [[gnu::always_inline]] friend Pack operator*(Pack a, Pack b) { return Pack{a.v * b.v}; }
	/// @brief @p a + @p b · @p c in each lane, as the scalar @ref mul_add.
	[[nodiscard]] [[gnu::always_inline]] friend Pack mul_add(Pack a, Pack b, Pack c) {
		if constexpr (kFusedMulAdd) {
			return Pack{a.v.MultiplyAddFused(b.v, c.v)};
		}
		else {
			return Pack{a.v.MultiplyAdd(b.v, c.v)};
		}
	}
	[[nodiscard]] [[gnu::always_inline]] friend Pack min(Pack a, Pack b) { return select(a > b, b, a); }
	[[nodiscard]] [[gnu::always_inline]] friend Pack max(Pack a, Pack b) { return select(a < b, b, a); }
	[[nodiscard]] [[gnu::always_inline]] friend Pack abs(Pack a) {
		// Clears the sign bit, as vabs does. Raw intrinsics: Argon's Absolute names a 64-bit vector's operation
		// differently from the one it calls, and SIMDe's vabs leaves the sign of -0.
		if constexpr (N == 2) {
			return Pack{
			    Vector{vreinterpret_f32_u32(vand_u32(vreinterpret_u32_f32(a.v.vec()), vdup_n_u32(0x7fffffffU)))}};
		}
		else {
			return Pack{
			    Vector{vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a.v.vec()), vdupq_n_u32(0x7fffffffU)))}};
		}
	}
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator<(Pack a, Pack b) { return {a.v.LessThan(b.v)}; }
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator>(Pack a, Pack b) { return {a.v.GreaterThan(b.v)}; }
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator<=(Pack a, Pack b) {
		return {a.v.LessThanOrEqual(b.v)};
	}
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator>=(Pack a, Pack b) {
		return {a.v.GreaterThanOrEqual(b.v)};
	}
	/// @brief @p a where @p m holds, @p b elsewhere.
	[[nodiscard]] [[gnu::always_inline]] friend Pack select(Mask<N> m, Pack a, Pack b) {
		return Pack{m.bits.Select(a.v, b.v)};
	}
	/// @brief The lanes' sum, lane 0 first, then each next lane added in order.
	[[nodiscard]] [[gnu::always_inline]] friend f32 sum(Pack a) {
#pragma clang fp contract(off) reassociate(off)
		f32 s = a.get(0);
		for (std::size_t i = 1; i < N; ++i) {
			s = s + a.get(i);
		}
		return s;
	}
};

/// @brief Transposes @p m in place: lane c of row r becomes lane r of row c.
template <std::size_t N>
[[gnu::always_inline]] inline void transpose(std::array<Pack<f32, N>, N>& m) {
	if constexpr (N == 2) {
		const float32x2x2_t t = vtrn_f32(m[0].v.vec(), m[1].v.vec());
		m[0] = Pack<f32, 2>{ArgonHalf<f32>{t.val[0]}};
		m[1] = Pack<f32, 2>{ArgonHalf<f32>{t.val[1]}};
	}
	else {
		const float32x4x2_t a = vtrnq_f32(m[0].v.vec(), m[1].v.vec());
		const float32x4x2_t b = vtrnq_f32(m[2].v.vec(), m[3].v.vec());
		m[0] = Pack<f32, 4>{Argon<f32>{vcombine_f32(vget_low_f32(a.val[0]), vget_low_f32(b.val[0]))}};
		m[1] = Pack<f32, 4>{Argon<f32>{vcombine_f32(vget_low_f32(a.val[1]), vget_low_f32(b.val[1]))}};
		m[2] = Pack<f32, 4>{Argon<f32>{vcombine_f32(vget_high_f32(a.val[0]), vget_high_f32(b.val[0]))}};
		m[3] = Pack<f32, 4>{Argon<f32>{vcombine_f32(vget_high_f32(a.val[1]), vget_high_f32(b.val[1]))}};
	}
}

} // namespace deluge::dsp
