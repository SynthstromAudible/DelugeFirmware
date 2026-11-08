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

#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

/// @file
/// @brief Pack<f32, 2> and Pack<f32, 4> on a NEON d or q register, and mul_add, fused where the target can.
///
/// **The scalar and the pack round alike.** Every float operation here, scalar or packed, is written in this file's
/// own function bodies, each opening with `#pragma clang fp contract(off) reassociate(off) reciprocal(off)`. Clang
/// fixes an instruction's float semantics in the function body that contains it, and inlining keeps them, so these
/// operations neither fuse, reorder nor take a reciprocal, whatever float flags the including file is built with (the
/// device's `-funsafe-math-optimizations` among them). That is why the pack's lanes are a GNU vector
/// (@ref detail::F32Lanes) whose `+`, `−` and `*` are written here: Argon's and arm_neon.h's operations, and SIMDe's on
/// a host, are bodies in their own headers, compiled under the including file's flags. On a NEON target the vector
/// operations are the NEON instructions. The fused @ref mul_add is the exact `fma` in either form.
///
/// The one flag the pragma cannot clear is no-signed-zeros (`#pragma float_control(precise, on)`, which would, is
/// ignored on 32-bit ARM). It licenses only rewrites that treat a zero's sign as insignificant, and the operations here
/// (+, −, ·, the comparisons, select; no division) carry a zero's sign into nothing but other zeros, so at worst a lane
/// and its scalar reference could differ in the sign of a zero; the unsafe-math specs
/// (tests/dsp/*_unsafe_math_spec.cpp) hold them bit for bit under the device's flags.
///
/// A lane of a pack's result is then bit for bit the scalar result under flush-to-zero (fp::FlushToZero), which ARMv7
/// NEON always applies. The scalar overloads here (mul_add, abs, sum) complete the set so that a kernel step written
/// over V compiles for f32. The pragmas are clang's: a compiler without them would contract by default.
namespace deluge::dsp {

namespace detail {
/// Whether the target has a fused float multiply-add instruction.
[[nodiscard]] consteval bool has_fused_mul_add() {
#if defined(__ARM_FEATURE_FMA) || defined(__FMA__)
	return true;
#else
	return false;
#endif
}
} // namespace detail

/// @brief Whether this target has a fused float multiply-add, which @ref mul_add then uses: AArch64, ARMv7 with VFPv4,
/// x86 with FMA. False on the Cortex-A9.
inline constexpr bool kFusedMulAdd = detail::has_fused_mul_add();

/// @brief @p a + @p b · @p c: one rounding where @ref kFusedMulAdd, two where not.
[[nodiscard]] [[gnu::always_inline]] inline f32 mul_add(f32 a, f32 b, f32 c) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
	if constexpr (kFusedMulAdd) {
		return std::fma(b, c, a);
	}
	else {
		return a + (b * c);
	}
}

/// @brief |@p x|: the scalar form of a pack's abs, which a step written over V calls.
/// @note Inside namespace deluge::dsp this hides the global integer ::abs for unqualified calls; an integer absolute
/// value there is std::abs.
[[nodiscard]] [[gnu::always_inline]] inline f32 abs(f32 x) {
	return std::fabs(x);
}

/// @brief @p x: the scalar form of a pack's lane sum, a one-lane pack's only lane.
[[nodiscard]] [[gnu::always_inline]] inline f32 sum(f32 x) {
	return x;
}

namespace detail {
/// N f32 lanes as a GNU vector: a q register for 4, a d register for 2.
template <std::size_t N>
using F32Lanes = f32 __attribute__((vector_size(N * sizeof(f32))));
/// The same lanes' bits.
template <std::size_t N>
using U32Lanes = uint32_t __attribute__((vector_size(N * sizeof(uint32_t))));
} // namespace detail

/// @brief The per-lane result of a comparison of two Pack<f32, N>, which @ref select reads.
template <std::size_t N>
requires(N == 2 || N == 4)
struct Mask {
	/// All ones in a lane where the comparison held, zero elsewhere: the vector comparison's own result type.
	decltype(detail::F32Lanes<N>{} < detail::F32Lanes<N>{}) bits;
};

/// @brief N f32 lanes on one NEON register.
/// @details min and max are `a > b ? b : a` and `a < b ? b : a`, as the scalar ones, not the hardware's, which differ
/// at equal operands (±0) and NaN.
template <std::size_t N>
requires(N == 2 || N == 4)
struct Pack<f32, N> {
	using Lanes = detail::F32Lanes<N>; ///< The vector.
	Lanes v;                           ///< The lanes.

	Pack() = default;
	/// @brief @p x in every lane, as Pack<T, N>'s converting constructor: a scalar operand broadcasts.
	template <std::same_as<f32> U>
	// A scalar converts implicitly, so that one expression compiles for a scalar and for a pack.
	// NOLINTNEXTLINE(google-explicit-constructor,cppcoreguidelines-explicit-constructor,hicpp-explicit-conversions)
	[[gnu::always_inline]] Pack(U x) {
		for (std::size_t i = 0; i < N; ++i) {
			v[i] = x;
		}
	}
	/// @brief The lanes of @p x.
	[[gnu::always_inline]] explicit Pack(Lanes x) : v{x} {}

	[[nodiscard]] [[gnu::always_inline]] static Pack broadcast(f32 x) { return Pack{x}; }
	/// @brief Lanes 0 … N−1 from @p p[0] … @p p[N−1].
	[[nodiscard]] [[gnu::always_inline]] static Pack load(const f32* p) {
		Pack r;
		std::memcpy(&r.v, p, sizeof(Lanes));
		return r;
	}
	/// @brief Lanes 0 … N−1 to @p p[0] … @p p[N−1].
	[[gnu::always_inline]] void store(f32* p) const { std::memcpy(p, &v, sizeof(Lanes)); }
	/// @brief Lane @p i.
	[[nodiscard]] [[gnu::always_inline]] f32 get(std::size_t i) const { return v[i]; }
	/// @brief Sets lane @p i to @p x.
	[[gnu::always_inline]] void set(std::size_t i, f32 x) { v[i] = x; }

	[[nodiscard]] [[gnu::always_inline]] friend Pack operator+(Pack a, Pack b) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		return Pack{a.v + b.v};
	}
	[[nodiscard]] [[gnu::always_inline]] friend Pack operator-(Pack a, Pack b) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		return Pack{a.v - b.v};
	}
	[[nodiscard]] [[gnu::always_inline]] friend Pack operator*(Pack a, Pack b) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		return Pack{a.v * b.v};
	}
	/// @brief @p a + @p b · @p c in each lane, as the scalar @ref mul_add.
	[[nodiscard]] [[gnu::always_inline]] friend Pack mul_add(Pack a, Pack b, Pack c) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		if constexpr (kFusedMulAdd) {
			return Pack{__builtin_elementwise_fma(b.v, c.v, a.v)};
		}
		else {
			return Pack{a.v + (b.v * c.v)};
		}
	}
	[[nodiscard]] [[gnu::always_inline]] friend Pack min(Pack a, Pack b) { return select(a > b, b, a); }
	[[nodiscard]] [[gnu::always_inline]] friend Pack max(Pack a, Pack b) { return select(a < b, b, a); }
	/// @brief |@p a|: the sign bit cleared, as std::fabs and vabs do.
	[[nodiscard]] [[gnu::always_inline]] friend Pack abs(Pack a) {
		using Bits = detail::U32Lanes<N>;
		return Pack{std::bit_cast<Lanes>(std::bit_cast<Bits>(a.v) & 0x7fffffffU)};
	}
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator<(Pack a, Pack b) { return {a.v < b.v}; }
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator>(Pack a, Pack b) { return {a.v > b.v}; }
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator<=(Pack a, Pack b) { return {a.v <= b.v}; }
	[[nodiscard]] [[gnu::always_inline]] friend Mask<N> operator>=(Pack a, Pack b) { return {a.v >= b.v}; }
	/// @brief @p a where @p m holds, @p b elsewhere.
	[[nodiscard]] [[gnu::always_inline]] friend Pack select(Mask<N> m, Pack a, Pack b) {
		return Pack{m.bits ? a.v : b.v};
	}
	/// @brief The lanes' sum, lane 0 first, then each next lane added in order.
	[[nodiscard]] [[gnu::always_inline]] friend f32 sum(Pack a) {
#pragma clang fp contract(off) reassociate(off) reciprocal(off)
		f32 s = a.v[0];
		for (std::size_t i = 1; i < N; ++i) {
			s = s + a.v[i];
		}
		return s;
	}
};

/// @brief Transposes @p m in place: lane c of row r becomes lane r of row c.
template <std::size_t N>
[[gnu::always_inline]] inline void transpose(std::array<Pack<f32, N>, N>& m) {
	using P = Pack<f32, N>;
	if constexpr (N == 2) {
		const auto r0 = __builtin_shufflevector(m[0].v, m[1].v, 0, 2);
		const auto r1 = __builtin_shufflevector(m[0].v, m[1].v, 1, 3);
		m[0] = P{r0};
		m[1] = P{r1};
	}
	else {
		// Rows a, b, c, d: transpose each 2×2 block of a with b and of c with d (vtrn), then pair the blocks' halves.
		const auto ab_even = __builtin_shufflevector(m[0].v, m[1].v, 0, 4, 2, 6); // a0 b0 a2 b2
		const auto ab_odd = __builtin_shufflevector(m[0].v, m[1].v, 1, 5, 3, 7);  // a1 b1 a3 b3
		const auto cd_even = __builtin_shufflevector(m[2].v, m[3].v, 0, 4, 2, 6); // c0 d0 c2 d2
		const auto cd_odd = __builtin_shufflevector(m[2].v, m[3].v, 1, 5, 3, 7);  // c1 d1 c3 d3
		m[0] = P{__builtin_shufflevector(ab_even, cd_even, 0, 1, 4, 5)};
		m[1] = P{__builtin_shufflevector(ab_odd, cd_odd, 0, 1, 4, 5)};
		m[2] = P{__builtin_shufflevector(ab_even, cd_even, 2, 3, 6, 7)};
		m[3] = P{__builtin_shufflevector(ab_odd, cd_odd, 2, 3, 6, 7)};
	}
}

} // namespace deluge::dsp
