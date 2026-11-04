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
#include <argon.hpp>
#include <argon/vectorize/first_n.hpp>
#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief NEON vectors of the fixed_point.h formats, and the named operations on them.
///
/// `FixedVec<Lane, N>` holds N lanes of one fixed_point.h type in one NEON register, with Argon as its storage. As
/// with the scalars, there are no arithmetic operators: each operation has the name of the scalar operation with the
/// same per-lane meaning, and compiles to the NEON instructions its comment names, mostly one (tests/unit-arm/codegen
/// checks them). Kernels keep `Argon` at their interfaces and convert with from_raw() and raw(); helpers private to one
/// family of kernels, such as the windowed sinc's SincWindow, may pass the vectors between themselves. load() and
/// store() move lanes between a vector and an array of its lane type.

namespace deluge::dsp {

namespace detail {

/// The Argon register that holds N lanes of Lane; only the pairs below have one.
template <class Lane, std::size_t N>
struct neon_register;
template <int F>
struct neon_register<Q<F>, 4> {
	using type = Argon<std::int32_t>;
};
template <int F>
struct neon_register<UQ<F>, 4> {
	using type = Argon<std::uint32_t>;
};
template <int F>
struct neon_register<Q16<F>, 8> {
	using type = Argon<std::int16_t>;
};
template <int F>
struct neon_register<Q16<F>, 4> {
	using type = ArgonHalf<std::int16_t>;
};
template <int F>
struct neon_register<Q16x2<F>, 4> {
	using type = Argon<std::uint32_t>;
};

// Wrapping lane adds and subtracts, one per register: the intrinsics, not the vector extension's operators, so that
// the signed forms are defined wrapping on every backend.
[[gnu::always_inline, nodiscard]] inline int32x4_t vec_add(int32x4_t a, int32x4_t b) noexcept {
	return vaddq_s32(a, b);
}
[[gnu::always_inline, nodiscard]] inline uint32x4_t vec_add(uint32x4_t a, uint32x4_t b) noexcept {
	return vaddq_u32(a, b);
}
[[gnu::always_inline, nodiscard]] inline int16x8_t vec_add(int16x8_t a, int16x8_t b) noexcept {
	return vaddq_s16(a, b);
}
[[gnu::always_inline, nodiscard]] inline int16x4_t vec_add(int16x4_t a, int16x4_t b) noexcept {
	return vadd_s16(a, b);
}
[[gnu::always_inline, nodiscard]] inline int32x4_t vec_sub(int32x4_t a, int32x4_t b) noexcept {
	return vsubq_s32(a, b);
}
[[gnu::always_inline, nodiscard]] inline uint32x4_t vec_sub(uint32x4_t a, uint32x4_t b) noexcept {
	return vsubq_u32(a, b);
}
[[gnu::always_inline, nodiscard]] inline int16x8_t vec_sub(int16x8_t a, int16x8_t b) noexcept {
	return vsubq_s16(a, b);
}
[[gnu::always_inline, nodiscard]] inline int16x4_t vec_sub(int16x4_t a, int16x4_t b) noexcept {
	return vsub_s16(a, b);
}

template <class Lane>
inline constexpr bool kIsQ16x2 = false;
template <int F>
inline constexpr bool kIsQ16x2<Q16x2<F>> = true;
template <class Lane>
inline constexpr bool kIsQ16 = false;
template <int F>
inline constexpr bool kIsQ16<Q16<F>> = true;

} // namespace detail

/// @brief N lanes of @p Lane, a fixed_point.h type, in one NEON register.
/// @tparam Lane each lane's type: Q<F>, UQ<F> or Q16x2<F> with N = 4; Q16<F> with N = 8 (a full register) or 4 (half).
/// @tparam N the lane count.
template <class Lane, std::size_t N>
class FixedVec {
	using storage_type = Lane::storage_type;
	static_assert(std::is_standard_layout_v<Lane> && sizeof(Lane) == sizeof(storage_type)
	              && alignof(Lane) == alignof(storage_type));

public:
	using lane_type = Lane;
	using register_type = detail::neon_register<Lane, N>::type; ///< The Argon register.
	static constexpr std::size_t kLanes = N;

private:
	/// Whether the register is a full Argon of the storage, which the partial loads and stores need.
	static constexpr bool kFullRegister = std::is_same_v<register_type, Argon<storage_type>>;

public:
	FixedVec() = default;

	/// @brief Wraps a register; the only way in.
	[[gnu::always_inline, nodiscard]] static FixedVec from_raw(register_type raw) noexcept {
		FixedVec v;
		v.raw_ = raw;
		return v;
	}
	/// @brief The register; the only way out.
	[[gnu::always_inline, nodiscard]] register_type raw() const noexcept { return raw_; }
	/// @brief @p x in every lane (`vdup`).
	[[gnu::always_inline, nodiscard]] static FixedVec splat(Lane x) noexcept {
		return from_raw(register_type{x.raw()});
	}
	/// @brief Lane @p i, through memory: for tests, not audio paths.
	[[nodiscard]] Lane lane(std::size_t i) const noexcept { return Lane::from_raw(raw_.to_array()[i]); }

	/// @brief Lanes 0…N−1 from the array at @p p (`vld1`).
	/// @details load() and store() are the one place an array of Lane is read and written as its storage. Lane is
	/// standard layout with its storage as its only member, so a Lane and its storage are pointer-interconvertible,
	/// and an array of Lane is laid out as an array of its storage (the static_asserts below pin both).
	[[gnu::always_inline, nodiscard]] static FixedVec load(const Lane* p) noexcept {
		return from_raw(register_type::Load(storage(p)));
	}
	/// @brief Lanes 0…active−1 from the array at @p p; the rest are zero and not read.
	/// @param active the lanes to load, 0 < active < N: a loop's last, partial step.
	[[gnu::always_inline, nodiscard]] static FixedVec load(const Lane* p, std::ptrdiff_t active) noexcept
	requires kFullRegister
	{
		return from_raw(argon::vectorize::detail::load_first_n(storage(p), static_cast<std::size_t>(active)));
	}
	/// @brief Lanes 0…N−1 to the array at @p p (`vst1`).
	[[gnu::always_inline]] void store(Lane* p) const noexcept { raw_.StoreTo(storage(p)); }
	/// @brief Lanes 0…active−1 to the array at @p p; the rest of the array is not written.
	/// @param active the lanes to store, 0 < active < N: a loop's last, partial step.
	[[gnu::always_inline]] void store(Lane* p, std::ptrdiff_t active) const noexcept
	requires kFullRegister
	{
		argon::vectorize::detail::store_first_n(storage(p), raw_, static_cast<std::size_t>(active));
	}

	/// @brief Each pair's low half: Q16x2Vec<F> → Q16Half<F> (`vmovn.i32`).
	[[gnu::always_inline, nodiscard]] auto lo() const noexcept
	requires detail::kIsQ16x2<Lane>
	{
		using Half = FixedVec<Q16<Lane::kFractionalBits>, 4>;
		return Half::from_raw(vreinterpret_s16_u16(vmovn_u32(raw_)));
	}
	/// @brief Each pair's high half: Q16x2Vec<F> → Q16Half<F> (`vshrn.i32 #16`).
	[[gnu::always_inline, nodiscard]] auto hi() const noexcept
	requires detail::kIsQ16x2<Lane>
	{
		using Half = FixedVec<Q16<Lane::kFractionalBits>, 4>;
		return Half::from_raw(vreinterpret_s16_u16(vshrn_n_u32(raw_, 16)));
	}
	/// @brief Lanes 0–3: Q16Vec<F> → Q16Half<F> (no instruction: the register's low d half).
	[[gnu::always_inline, nodiscard]] auto lo() const noexcept
	requires(detail::kIsQ16<Lane> && N == 8)
	{
		return FixedVec<Lane, 4>::from_raw(vget_low_s16(raw_));
	}
	/// @brief Lanes 4–7: Q16Vec<F> → Q16Half<F> (no instruction: the register's high d half).
	[[gnu::always_inline, nodiscard]] auto hi() const noexcept
	requires(detail::kIsQ16<Lane> && N == 8)
	{
		return FixedVec<Lane, 4>::from_raw(vget_high_s16(raw_));
	}

private:
	[[gnu::always_inline]] static const storage_type* storage(const Lane* p) noexcept {
		return reinterpret_cast<const storage_type*>(p);
	}
	[[gnu::always_inline]] static storage_type* storage(Lane* p) noexcept { return reinterpret_cast<storage_type*>(p); }

	register_type raw_;
};

template <int F>
using QVec = FixedVec<Q<F>, 4>; ///< four Q<F> (int32x4_t)
template <int F>
using UQVec = FixedVec<UQ<F>, 4>; ///< four UQ<F> (uint32x4_t); UQVec<32> is four oscillator phases
template <int F>
using Q16Vec = FixedVec<Q16<F>, 8>; ///< eight Q16<F> (int16x8_t)
template <int F>
using Q16Half = FixedVec<Q16<F>, 4>; ///< four Q16<F> (int16x4_t), a half register
template <int F>
using Q16x2Vec = FixedVec<Q16x2<F>, 4>; ///< four {lo, hi} Q16<F> pairs, one per 32-bit lane (uint32x4_t)

/// @brief a + b per lane, wrapping (`vadd`).
template <class S, int F, std::size_t N>
[[gnu::always_inline, nodiscard]] inline FixedVec<Fixed<S, F>, N> add_wrap(FixedVec<Fixed<S, F>, N> a,
                                                                           FixedVec<Fixed<S, F>, N> b) noexcept {
	return FixedVec<Fixed<S, F>, N>::from_raw(detail::vec_add(a.raw(), b.raw()));
}
/// @brief a − b per lane, wrapping (`vsub`).
template <class S, int F, std::size_t N>
[[gnu::always_inline, nodiscard]] inline FixedVec<Fixed<S, F>, N> sub_wrap(FixedVec<Fixed<S, F>, N> a,
                                                                           FixedVec<Fixed<S, F>, N> b) noexcept {
	return FixedVec<Fixed<S, F>, N>::from_raw(detail::vec_sub(a.raw(), b.raw()));
}

/// @brief sat((2 × a × b) >> 32) per lane: Q<A> × Q<B> → Q<A+B−31> (`vqdmulh.s32`). Only min × min saturates.
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B - 31> mul_doubling_sat(QVec<A> a, QVec<B> b) noexcept {
	return QVec<A + B - 31>::from_raw(vqdmulhq_s32(a.raw(), b.raw()));
}
/// @brief sat((2 × a × b) >> 32) per lane, by one scalar: Q<A> × Q<B> → Q<A+B−31> (`vdup.32`, `vqdmulh.s32`; a
/// loop hoists the `vdup`).
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B - 31> mul_doubling_sat(QVec<A> a, Q<B> b) noexcept {
	return QVec<A + B - 31>::from_raw(vqdmulhq_n_s32(a.raw(), b.raw()));
}
/// @brief sat((2 × a × b) >> 32) per lane, by lane @p I of @p b: Q<A> × Q<B> → Q<A+B−31> (`vqdmulh.s32` by a
/// d-register lane). Only lane I of @p b is read, so its other lanes may hold other formats.
template <int I, int A, int B>
requires(I >= 0 && I < 4 && A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B - 31> mul_doubling_sat_lane(QVec<A> a, QVec<B> b) noexcept {
	const int32x4_t lanes = b.raw();
	if constexpr (I < 2) {
		return QVec<A + B - 31>::from_raw(vqdmulhq_lane_s32(a.raw(), vget_low_s32(lanes), I));
	}
	else {
		return QVec<A + B - 31>::from_raw(vqdmulhq_lane_s32(a.raw(), vget_high_s32(lanes), I - 2));
	}
}
/// @brief sat((2 × a × b) >> 16) per lane, by one scalar: Q16<A> × Q16<B> → Q16<A+B−15> (`vqdmulh.s16`). Only
/// −32768 × −32768 saturates. A loop hoists the `vdup.16` of b.
template <int A, int B>
requires(A + B >= 15 && A + B - 15 <= 15)
[[gnu::always_inline, nodiscard]] inline Q16Vec<A + B - 15> mul_doubling_sat(Q16Vec<A> a, Q16<B> b) noexcept {
	return Q16Vec<A + B - 15>::from_raw(vqdmulhq_n_s16(a.raw(), b.raw()));
}
/// @brief sat((2 × a × b + 2^31) >> 32) per lane: Q<A> × Q<B> → Q<A+B−31> (`vqrdmulh.s32`).
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B - 31> mul_doubling_r_sat(QVec<A> a, QVec<B> b) noexcept {
	return QVec<A + B - 31>::from_raw(vqrdmulhq_s32(a.raw(), b.raw()));
}
/// @brief sat((2 × a × b + 2^31) >> 32) per lane, by one scalar: Q<A> × Q<B> → Q<A+B−31> (`vdup.32`,
/// `vqrdmulh.s32`; a loop hoists the `vdup`).
template <int A, int B>
requires(A + B >= 31 && A + B - 31 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B - 31> mul_doubling_r_sat(QVec<A> a, Q<B> b) noexcept {
	return QVec<A + B - 31>::from_raw(vqrdmulhq_n_s32(a.raw(), b.raw()));
}

/// @brief x × 2^N per lane in the same format, wrapping (`vshl.i32 #N`).
template <int N, int F>
requires detail::shift_amount<N, std::int32_t>
[[gnu::always_inline, nodiscard]] inline QVec<F> scale_shl_wrap(QVec<F> x) noexcept {
	return QVec<F>::from_raw(vshlq_n_s32(x.raw(), N));
}
/// @brief x / 2^N per lane in the same format, rounding toward −∞ (`vshr.s32 #N`).
template <int N, int F>
requires detail::shift_amount<N, std::int32_t>
[[gnu::always_inline, nodiscard]] inline QVec<F> scale_shr_floor(QVec<F> x) noexcept {
	return QVec<F>::from_raw(vshrq_n_s32(x.raw(), N));
}

/// @brief acc + x / 2^N per lane in the same format: the shift rounds toward −∞, the add wraps (`vsra.s32 #N`).
template <int N, int F>
requires detail::shift_amount<N, std::int32_t>
[[gnu::always_inline, nodiscard]] inline QVec<F> add_scale_shr_floor(QVec<F> acc, QVec<F> x) noexcept {
	return QVec<F>::from_raw(vsraq_n_s32(acc.raw(), x.raw(), N));
}

/// @brief Same value per lane, fewer fractional bits, rounding toward −∞: QVec<F> → QVec<T> (`vshr.s32 #(F−T)`).
template <class To, int F>
requires std::is_same_v<To, QVec<To::lane_type::kFractionalBits>> && (To::lane_type::kFractionalBits < F)
[[gnu::always_inline, nodiscard]] inline To as_floor(QVec<F> x) noexcept {
	return To::from_raw(vshrq_n_s32(x.raw(), F - To::lane_type::kFractionalBits));
}

/// @brief Same value per lane, more fractional bits, wrapping: QVec<F> → QVec<T> (`vshl.i32 #(T−F)`).
template <class To, int F>
requires std::is_same_v<To, QVec<To::lane_type::kFractionalBits>> && (To::lane_type::kFractionalBits > F)
[[gnu::always_inline, nodiscard]] inline To as_wrap(QVec<F> x) noexcept {
	return To::from_raw(vshlq_n_s32(x.raw(), To::lane_type::kFractionalBits - F));
}

/// @brief Each lane held to an N-bit signed word, [−2^(N−1), 2^(N−1)), in the same format (`vmax.s32`, `vmin.s32`).
template <int N, int F>
requires(N >= 1 && N <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<F> clamp_bits(QVec<F> x) noexcept {
	return QVec<F>::from_raw(vminq_s32(vmaxq_s32(x.raw(), vdupq_n_s32(-(std::int32_t{1} << (N - 1)))),
	                                   vdupq_n_s32((std::int32_t{1} << (N - 1)) - 1)));
}

/// @brief The same bits read as signed: UQVec<F> → QVec<T> (no instruction). A saw is its phase read as Q31.
template <class To, int F>
requires std::is_same_v<To, QVec<To::lane_type::kFractionalBits>>
[[gnu::always_inline, nodiscard]] inline To reinterpret_bits(UQVec<F> x) noexcept {
	return To::from_raw(vreinterpretq_s32_u32(x.raw()));
}
/// @brief The same bits read in another format: QVec<F> → QVec<T> (no instruction), as the scalar reinterpret_bits.
template <class To, int F>
requires std::is_same_v<To, QVec<To::lane_type::kFractionalBits>>
[[gnu::always_inline, nodiscard]] inline To reinterpret_bits(QVec<F> x) noexcept {
	return To::from_raw(x.raw());
}

/// @brief A 16-bit value per lane in the top half of 32 bits: Q16Half<F> → QVec<F+16>; exact (`vshll.i16 #16`, the
/// full-width form).
template <int F>
[[gnu::always_inline, nodiscard]] inline QVec<F + 16> from_q16(Q16Half<F> x) noexcept {
	return QVec<F + 16>::from_raw(vshll_n_s16(x.raw(), 16));
}
/// @brief sat(2 × a × b) per lane, widened: Q16<A> × Q16<B> → Q<A+B+1> (`vqdmull.s16`). Q15 × Q15 is Q31; only
/// −32768 × −32768 saturates.
template <int A, int B>
requires(A + B + 1 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B + 1> mul_doubling_wide_sat(Q16Half<A> a, Q16Half<B> b) noexcept {
	return QVec<A + B + 1>::from_raw(vqdmull_s16(a.raw(), b.raw()));
}
/// @brief sat(acc + sat(2 × a × b)) per lane, widened: the doubled product saturates, then the sum
/// (`vqdmlal.s16`).
template <int A, int B>
requires(A + B + 1 <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B + 1> mla_doubling_wide_sat(QVec<A + B + 1> acc, Q16Half<A> a,
                                                                               Q16Half<B> b) noexcept {
	return QVec<A + B + 1>::from_raw(vqdmlal_s16(acc.raw(), a.raw(), b.raw()));
}
/// @brief acc + a × b per lane, widened and wrapping: Q16<A> × Q16<B> → Q<A+B> (`vmlal.s16`).
template <int A, int B>
requires(A + B <= 31)
[[gnu::always_inline, nodiscard]] inline QVec<A + B> mla_wide(QVec<A + B> acc, Q16Half<A> a, Q16Half<B> b) noexcept {
	return QVec<A + B>::from_raw(vmlal_s16(acc.raw(), a.raw(), b.raw()));
}

/// @brief The four lanes' sum, wrapping: QVec<F> → Q<F> (`vadd.i32` of the halves, `vpadd.i32`, `vmov`).
template <int F>
[[gnu::always_inline, nodiscard]] inline Q<F> sum_wrap(QVec<F> x) noexcept {
	const int32x4_t v = x.raw();
	const int32x2_t halves = vadd_s32(vget_low_s32(v), vget_high_s32(v));
	return Q<F>::from_raw(vget_lane_s32(vpadd_s32(halves, halves), 0));
}

/// @brief Each lane as a float, rounded to nearest once, as the scalar to_float (`vcvt.f32.s32 #F`; plain `vcvt` for
/// F = 0).
template <int F>
requires(F >= 0 && F <= 32)
[[gnu::always_inline, nodiscard]] inline Argon<float> to_float(QVec<F> x) noexcept {
	const Argon<std::int32_t> lanes{x.raw()};
	if constexpr (F == 0) {
		return lanes.ConvertTo<float>();
	}
	else {
		return lanes.ConvertTo<float, F>();
	}
}
/// @brief Each float lane as Q<F>, as the scalar from_float: truncating toward zero, saturating, NaN to 0
/// (`vcvt.s32.f32 #F`; plain `vcvt` for F = 0).
template <int F>
requires(F >= 0 && F <= 32)
[[gnu::always_inline, nodiscard]] inline QVec<F> from_float(Argon<float> x) noexcept {
	if constexpr (F == 0) {
		return QVec<F>::from_raw(x.ConvertTo<std::int32_t>());
	}
	else {
		return QVec<F>::from_raw(x.ConvertTo<std::int32_t, F>());
	}
}

} // namespace deluge::dsp
