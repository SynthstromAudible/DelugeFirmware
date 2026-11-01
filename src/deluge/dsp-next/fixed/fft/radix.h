/*
 * Derived from Ne10's int32 FFT kernels (modules/dsp/NE10_fft_int32.neonintrinsic.c), under its licence:
 *
 *   Copyright 2013-16 ARM Limited and Contributors.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of ARM Limited nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY ARM LIMITED AND CONTRIBUTORS "AS IS" AND
 *   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL ARM LIMITED AND CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/fft/complex.h"

#include <argon.hpp>
#include <array>
#include <cstdint>
#include <span>
#include <utility>

/// @file
/// @brief The int32 FFT's radix kernels, stage loop and real splits. Every sum and difference wraps. Every vector
/// product is the saturating doubling high half, truncating except in the 16-point inverse, which rounds. The scalar
/// 8-point paths differ: the 8-point inverse multiplies by √½ as a 64-bit product shifted down by 31, and its real
/// split sums two 64-bit products' high words and then doubles the sum, both wrapping. Each kernel computes its values
/// from the same operands in the same order the recorded transforms were made with, so they match bit for bit.
namespace deluge::dsp::fixed::fft::detail {

enum class Direction : uint8_t { FORWARD, INVERSE };

using V = Argon<int32_t>;

/// @brief Four complex values de-interleaved: their real parts and their imaginary parts.
struct Pair {
	V r;
	V i;
};

/// @brief Two vectors a permutation returns, unpacked without indexing the array.
struct Two {
	V first;
	V second;
};

[[nodiscard, gnu::always_inline]] inline Pair load2(const int32_t* p) {
	const std::array<V, 2> ri = V::LoadInterleaved<2>(p);
	static_assert(sizeof(Pair) == sizeof(ri));
	return __builtin_bit_cast(Pair, ri);
}

[[gnu::always_inline]] inline void store2(int32_t* p, Pair x) {
	argon::store_interleaved(p, x.r, x.i);
}

[[nodiscard, gnu::always_inline]] inline Two two(std::array<V, 2> v) {
	static_assert(sizeof(Two) == sizeof(v));
	return __builtin_bit_cast(Two, v);
}

[[nodiscard, gnu::always_inline]] inline const int32_t* words(const Complex* c) {
	return reinterpret_cast<const int32_t*>(c);
}

[[nodiscard, gnu::always_inline]] inline int32_t* words(Complex* c) {
	return reinterpret_cast<int32_t*>(c);
}

/// @brief Four butterflies' outputs, one per lane of each, regrouped so that each of the four results holds one
///        butterfly's four outputs in order: the 4×4 transpose a stage's lanes take on their way to memory.
[[nodiscard, gnu::always_inline]] inline std::array<Pair, 4> transpose(Pair o0, Pair o1, Pair o2, Pair o3) {
	const Two r01 = two(o0.r.TransposeWith(o1.r));
	const Two i01 = two(o0.i.TransposeWith(o1.i));
	const Two r23 = two(o2.r.TransposeWith(o3.r));
	const Two i23 = two(o2.i.TransposeWith(o3.i));
	return {{
	    {V::Combine(r01.first.GetLow(), r23.first.GetLow()), V::Combine(i01.first.GetLow(), i23.first.GetLow())},
	    {V::Combine(r01.second.GetLow(), r23.second.GetLow()), V::Combine(i01.second.GetLow(), i23.second.GetLow())},
	    {V::Combine(r01.first.GetHigh(), r23.first.GetHigh()), V::Combine(i01.first.GetHigh(), i23.first.GetHigh())},
	    {V::Combine(r01.second.GetHigh(), r23.second.GetHigh()),
	     V::Combine(i01.second.GetHigh(), i23.second.GetHigh())},
	}};
}

/// @brief (@p x · √½) in Q31: the product of the wrapped 32-bit value in 64 bits, shifted down and truncated.
[[nodiscard, gnu::always_inline]] inline int32_t times_sqrt_half(int32_t x) {
	constexpr int64_t kSqrtHalf = 1518500249; // √½ in Q31
	return static_cast<int32_t>((static_cast<int64_t>(x) * kSqrtHalf) >> 31);
}

/// @brief The high word of the 64-bit product @p a × @p b.
[[nodiscard, gnu::always_inline]] inline int32_t high_product(int32_t a, int32_t b) {
	return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 32);
}

/// @brief @p x doubled, wrapping.
[[nodiscard, gnu::always_inline]] inline int32_t doubled(int32_t x) {
	return static_cast<int32_t>(static_cast<uint32_t>(x) << 1);
}

/// @brief Writes complex value @p k of an interleaved (r, i) output: the caller's sample words, written as words.
[[gnu::always_inline]] inline void put(int32_t* out, int32_t k, int32_t r, int32_t i) {
	out[2 * k] = r;
	out[(2 * k) + 1] = i;
}

/// @brief The 4-point inverse complex transform.
[[gnu::always_inline]] inline void fft4_inverse(int32_t* out, const Complex* in) {
	const int32_t s2_r = wrapping_sub(in[0].r, in[2].r);
	const int32_t s2_i = wrapping_sub(in[0].i, in[2].i);
	const int32_t tmp_r = wrapping_add(in[0].r, in[2].r);
	const int32_t tmp_i = wrapping_add(in[0].i, in[2].i);
	const int32_t s0_r = wrapping_add(in[1].r, in[3].r);
	const int32_t s0_i = wrapping_add(in[1].i, in[3].i);
	const int32_t s1_r = wrapping_sub(in[1].r, in[3].r);
	const int32_t s1_i = wrapping_sub(in[1].i, in[3].i);
	put(out, 2, wrapping_sub(tmp_r, s0_r), wrapping_sub(tmp_i, s0_i));
	put(out, 0, wrapping_add(tmp_r, s0_r), wrapping_add(tmp_i, s0_i));
	put(out, 1, wrapping_sub(s2_r, s1_i), wrapping_add(s2_i, s1_r));
	put(out, 3, wrapping_add(s2_r, s1_i), wrapping_sub(s2_i, s1_r));
}

/// @brief The 8-point inverse complex transform.
[[gnu::always_inline]] inline void fft8_inverse(int32_t* out, const Complex* in) {
	const int32_t s0_r = wrapping_add(in[0].r, in[4].r);
	const int32_t s0_i = wrapping_add(in[0].i, in[4].i);
	const int32_t s1_r = wrapping_sub(in[0].r, in[4].r);
	const int32_t s1_i = wrapping_sub(in[0].i, in[4].i);
	const int32_t s2_r = wrapping_add(in[1].r, in[5].r);
	const int32_t s2_i = wrapping_add(in[1].i, in[5].i);
	const int32_t s3_r = wrapping_sub(in[1].r, in[5].r);
	const int32_t s3_i = wrapping_sub(in[1].i, in[5].i);
	const int32_t s4_r = wrapping_add(in[2].r, in[6].r);
	const int32_t s4_i = wrapping_add(in[2].i, in[6].i);
	const int32_t s5_r = wrapping_sub(in[2].r, in[6].r);
	const int32_t s5_i = wrapping_sub(in[2].i, in[6].i);
	const int32_t s6_r = wrapping_add(in[3].r, in[7].r);
	const int32_t s6_i = wrapping_add(in[3].i, in[7].i);
	const int32_t s7_r = wrapping_sub(in[3].r, in[7].r);
	const int32_t s7_i = wrapping_sub(in[3].i, in[7].i);

	int32_t t0_r = wrapping_sub(s0_r, s4_r);
	int32_t t0_i = wrapping_sub(s0_i, s4_i);
	int32_t t1_r = wrapping_add(s0_r, s4_r);
	int32_t t1_i = wrapping_add(s0_i, s4_i);
	int32_t t2_r = wrapping_add(s2_r, s6_r);
	int32_t t2_i = wrapping_add(s2_i, s6_i);
	int32_t t3_r = wrapping_sub(s2_r, s6_r);
	int32_t t3_i = wrapping_sub(s2_i, s6_i);
	put(out, 0, wrapping_add(t1_r, t2_r), wrapping_add(t1_i, t2_i));
	put(out, 4, wrapping_sub(t1_r, t2_r), wrapping_sub(t1_i, t2_i));
	put(out, 2, wrapping_sub(t0_r, t3_i), wrapping_add(t0_i, t3_r));
	put(out, 6, wrapping_add(t0_r, t3_i), wrapping_sub(t0_i, t3_r));

	// The odd outputs: the ±45° terms rotate by √½ (1 + i) and its conjugates.
	const int32_t t4_r = times_sqrt_half(wrapping_sub(s3_r, s3_i));
	const int32_t t4_i = times_sqrt_half(wrapping_add(s3_r, s3_i));
	const int32_t t5_r = times_sqrt_half(wrapping_add(s7_r, s7_i));
	const int32_t t5_i = wrapping_sub(0, times_sqrt_half(wrapping_sub(s7_r, s7_i)));
	t0_r = wrapping_add(s1_r, s5_i);
	t0_i = wrapping_sub(s1_i, s5_r);
	t1_r = wrapping_sub(s1_r, s5_i);
	t1_i = wrapping_add(s1_i, s5_r);
	t2_r = wrapping_sub(t4_r, t5_r);
	t2_i = wrapping_sub(t4_i, t5_i);
	t3_r = wrapping_add(t4_r, t5_r);
	t3_i = wrapping_add(t4_i, t5_i);
	put(out, 1, wrapping_add(t1_r, t2_r), wrapping_add(t1_i, t2_i));
	put(out, 5, wrapping_sub(t1_r, t2_r), wrapping_sub(t1_i, t2_i));
	put(out, 3, wrapping_sub(t0_r, t3_i), wrapping_add(t0_i, t3_r));
	put(out, 7, wrapping_add(t0_r, t3_i), wrapping_sub(t0_i, t3_r));
}

/// @brief The 16-point inverse complex transform: a radix-4 stage without twiddles, a lane transpose, and a radix-4
///        stage rotating by @p tw (three rows of four), whose products round.
[[gnu::always_inline]] inline void fft16_inverse(Complex* out, const Complex* in, const Complex* tw) {
	const Pair in0123 = load2(words(in));
	const Pair in4567 = load2(words(in + 4));
	const Pair in89ab = load2(words(in + 8));
	const Pair incdef = load2(words(in + 12));
	const V t2_r = in0123.r.Subtract(in89ab.r);
	const V t2_i = in0123.i.Subtract(in89ab.i);
	const V t3_r = in0123.r.Add(in89ab.r);
	const V t3_i = in0123.i.Add(in89ab.i);
	const V t0_r = in4567.r.Add(incdef.r);
	const V t0_i = in4567.i.Add(incdef.i);
	const V t1_r = in4567.r.Subtract(incdef.r);
	const V t1_i = in4567.i.Subtract(incdef.i);
	const V r26ae = t3_r.Subtract(t0_r);
	const V i26ae = t3_i.Subtract(t0_i);
	const V r048c = t3_r.Add(t0_r);
	const V i048c = t3_i.Add(t0_i);
	const V r159d = t2_r.Subtract(t1_i);
	const V i159d = t2_i.Add(t1_r);
	const V r37bf = t2_r.Add(t1_i);
	const V i37bf = t2_i.Subtract(t1_r);

	const Two z0 = two(r048c.ZipWith(r159d));
	const Two z1 = two(i048c.ZipWith(i159d));
	const Two z2 = two(r26ae.ZipWith(r37bf));
	const Two z3 = two(i26ae.ZipWith(i37bf));
	const V in_r0123 = V::Combine(z0.first.GetLow(), z2.first.GetLow());
	const V in_i0123 = V::Combine(z1.first.GetLow(), z3.first.GetLow());
	const V in_r4567 = V::Combine(z0.first.GetHigh(), z2.first.GetHigh());
	const V in_i4567 = V::Combine(z1.first.GetHigh(), z3.first.GetHigh());
	const V in_r89ab = V::Combine(z0.second.GetLow(), z2.second.GetLow());
	const V in_i89ab = V::Combine(z1.second.GetLow(), z3.second.GetLow());
	const V in_rcdef = V::Combine(z0.second.GetHigh(), z2.second.GetHigh());
	const V in_icdef = V::Combine(z1.second.GetHigh(), z3.second.GetHigh());
	const Pair tw1 = load2(words(tw));
	const Pair tw2 = load2(words(tw + 4));
	const Pair tw3 = load2(words(tw + 8));

	V s0_r = in_r4567.MultiplyRoundFixedQMax(tw1.r);
	V s0_i = in_i4567.MultiplyRoundFixedQMax(tw1.r);
	V s1_r = in_r89ab.MultiplyRoundFixedQMax(tw2.r);
	V s1_i = in_i89ab.MultiplyRoundFixedQMax(tw2.r);
	V s2_r = in_rcdef.MultiplyRoundFixedQMax(tw3.r);
	V s2_i = in_icdef.MultiplyRoundFixedQMax(tw3.r);
	const V tmp0 = in_i4567.MultiplyRoundFixedQMax(tw1.i);
	const V tmp1 = in_r4567.MultiplyRoundFixedQMax(tw1.i);
	const V tmp2 = in_i89ab.MultiplyRoundFixedQMax(tw2.i);
	const V tmp3 = in_r89ab.MultiplyRoundFixedQMax(tw2.i);
	const V tmp4 = in_icdef.MultiplyRoundFixedQMax(tw3.i);
	const V tmp5 = in_rcdef.MultiplyRoundFixedQMax(tw3.i);
	s0_r = s0_r.Add(tmp0);
	s0_i = s0_i.Subtract(tmp1);
	s1_r = s1_r.Add(tmp2);
	s1_i = s1_i.Subtract(tmp3);
	s2_r = s2_r.Add(tmp4);
	s2_i = s2_i.Subtract(tmp5);

	const V s5_r = in_r0123.Subtract(s1_r);
	const V s5_i = in_i0123.Subtract(s1_i);
	const V sum_r = in_r0123.Add(s1_r);
	const V sum_i = in_i0123.Add(s1_i);
	const V s3_r = s0_r.Add(s2_r);
	const V s3_i = s0_i.Add(s2_i);
	const V s4_r = s0_r.Subtract(s2_r);
	const V s4_i = s0_i.Subtract(s2_i);
	const Pair out89ab{sum_r.Subtract(s3_r), sum_i.Subtract(s3_i)};
	const Pair out0123{sum_r.Add(s3_r), sum_i.Add(s3_i)};
	const Pair out4567{s5_r.Subtract(s4_i), s5_i.Add(s4_r)};
	const Pair outcdef{s5_r.Add(s4_i), s5_i.Subtract(s4_r)};
	store2(words(out), out0123);
	store2(words(out + 4), out4567);
	store2(words(out + 8), out89ab);
	store2(words(out + 12), outcdef);
}

/// @brief A first stage of radix 8, four butterflies per pass: inputs @p stride apart, outputs in groups of eight.
template <Direction D>
[[gnu::always_inline]] inline void radix8x4(Complex* out, const Complex* in, int32_t stride) {
	const auto* src = words(in);
	int32_t* dst = words(out);
	const int32_t src_step = stride * 2;
	const V tw_81{1518500249};   // √½ in Q31
	const V tw_81n{-1518500249}; // −√½
	for (int32_t f = 0; f < stride; f += 4) {
		const Pair in0 = load2(src);
		const Pair in2 = load2(src + src_step);
		const Pair in4 = load2(src + (2 * src_step));
		const Pair in6 = load2(src + (3 * src_step));
		const Pair in1 = load2(src + (4 * src_step));
		const Pair in3 = load2(src + (5 * src_step));
		const Pair in5 = load2(src + (6 * src_step));
		const Pair in7 = load2(src + (7 * src_step));
		src += 8;

		const V sin0_r = in0.r.Add(in1.r);
		const V sin0_i = in0.i.Add(in1.i);
		const V sin1_r = in0.r.Subtract(in1.r);
		const V sin1_i = in0.i.Subtract(in1.i);
		const V sin2_r = in2.r.Add(in3.r);
		const V sin2_i = in2.i.Add(in3.i);
		const V sin3_r = in2.r.Subtract(in3.r);
		const V sin3_i = in2.i.Subtract(in3.i);
		const V sin4_r = in4.r.Add(in5.r);
		const V sin4_i = in4.i.Add(in5.i);
		const V sin5_r = in4.r.Subtract(in5.r);
		const V sin5_i = in4.i.Subtract(in5.i);
		const V sin6_r = in6.r.Add(in7.r);
		const V sin6_i = in6.i.Add(in7.i);
		const V sin7_r = in6.r.Subtract(in7.r);
		const V sin7_i = in6.i.Subtract(in7.i);

		// The odd inputs' rotations: by ∓i, and by √½ (1 ∓ i) and √½ (−1 ∓ i).
		V s5_r;
		V s5_i;
		V s3_r;
		V s3_i;
		V s7_r;
		V s7_i;
		if constexpr (D == Direction::FORWARD) {
			s5_r = sin5_i;
			s5_i = sin5_r.Negate();
			s3_r = sin3_r.Add(sin3_i);
			s3_i = sin3_i.Subtract(sin3_r);
			s7_r = sin7_r.Subtract(sin7_i);
			s7_i = sin7_i.Add(sin7_r);
		}
		else {
			s5_r = sin5_i.Negate();
			s5_i = sin5_r;
			s3_r = sin3_r.Subtract(sin3_i);
			s3_i = sin3_i.Add(sin3_r);
			s7_r = sin7_r.Add(sin7_i);
			s7_i = sin7_i.Subtract(sin7_r);
		}
		s3_r = s3_r.MultiplyFixedQMax(tw_81);
		s3_i = s3_i.MultiplyFixedQMax(tw_81);
		s7_r = s7_r.MultiplyFixedQMax(tw_81n);
		s7_i = s7_i.MultiplyFixedQMax(tw_81n);

		const V s8_r = sin0_r.Add(sin4_r);
		const V s8_i = sin0_i.Add(sin4_i);
		const V s9_r = sin1_r.Add(s5_r);
		const V s9_i = sin1_i.Add(s5_i);
		const V s10_r = sin0_r.Subtract(sin4_r);
		const V s10_i = sin0_i.Subtract(sin4_i);
		const V s11_r = sin1_r.Subtract(s5_r);
		const V s11_i = sin1_i.Subtract(s5_i);
		const V s12_r = sin2_r.Add(sin6_r);
		const V s12_i = sin2_i.Add(sin6_i);
		const V s13_r = s3_r.Add(s7_r);
		const V s13_i = s3_i.Add(s7_i);
		const V s14_r = sin2_r.Subtract(sin6_r);
		const V s14_i = sin2_i.Subtract(sin6_i);
		const V s15_r = s3_r.Subtract(s7_r);
		const V s15_i = s3_i.Subtract(s7_i);
		const Pair out4{s8_r.Subtract(s12_r), s8_i.Subtract(s12_i)};
		const Pair out5{s9_r.Subtract(s13_r), s9_i.Subtract(s13_i)};
		const Pair out0{s8_r.Add(s12_r), s8_i.Add(s12_i)};
		const Pair out1{s9_r.Add(s13_r), s9_i.Add(s13_i)};
		Pair out2;
		Pair out3;
		Pair out6;
		Pair out7;
		if constexpr (D == Direction::FORWARD) {
			out2 = {s10_r.Add(s14_i), s10_i.Subtract(s14_r)};
			out3 = {s11_r.Add(s15_i), s11_i.Subtract(s15_r)};
			out6 = {s10_r.Subtract(s14_i), s10_i.Add(s14_r)};
			out7 = {s11_r.Subtract(s15_i), s11_i.Add(s15_r)};
		}
		else {
			out2 = {s10_r.Subtract(s14_i), s10_i.Add(s14_r)};
			out3 = {s11_r.Subtract(s15_i), s11_i.Add(s15_r)};
			out6 = {s10_r.Add(s14_i), s10_i.Subtract(s14_r)};
			out7 = {s11_r.Add(s15_i), s11_i.Subtract(s15_r)};
		}

		const std::array<Pair, 4> low = transpose(out0, out1, out2, out3);
		const std::array<Pair, 4> high = transpose(out4, out5, out6, out7);
		store2(dst, low[0]);
		store2(dst + 8, high[0]);
		store2(dst + 16, low[1]);
		store2(dst + 24, high[1]);
		store2(dst + 32, low[2]);
		store2(dst + 40, high[2]);
		store2(dst + 48, low[3]);
		store2(dst + 56, high[3]);
		dst += 64;
	}
}

/// @brief A first stage of radix 4, four butterflies per pass, without twiddles: inputs @p stride apart, outputs in
///        groups of four.
template <Direction D>
[[gnu::always_inline]] inline void radix4x4_first(Complex* out, const Complex* in, int32_t stride) {
	const auto* src = words(in);
	int32_t* dst = words(out);
	const int32_t src_step = stride * 2;
	for (int32_t f = 0; f < stride; f += 4) {
		const Pair in0 = load2(src);
		const Pair in1 = load2(src + src_step);
		const Pair in2 = load2(src + (2 * src_step));
		const Pair in3 = load2(src + (3 * src_step));
		src += 8;
		const V s0_r = in0.r.Add(in2.r);
		const V s0_i = in0.i.Add(in2.i);
		const V s1_r = in0.r.Subtract(in2.r);
		const V s1_i = in0.i.Subtract(in2.i);
		const V s2_r = in1.r.Add(in3.r);
		const V s2_i = in1.i.Add(in3.i);
		const V s3_r = in1.r.Subtract(in3.r);
		const V s3_i = in1.i.Subtract(in3.i);
		const Pair out2{s0_r.Subtract(s2_r), s0_i.Subtract(s2_i)};
		const Pair out0{s0_r.Add(s2_r), s0_i.Add(s2_i)};
		const Pair out1 = D == Direction::FORWARD ? Pair{s1_r.Add(s3_i), s1_i.Subtract(s3_r)}
		                                          : Pair{s1_r.Subtract(s3_i), s1_i.Add(s3_r)};
		const Pair out3 = D == Direction::FORWARD ? Pair{s1_r.Subtract(s3_i), s1_i.Add(s3_r)}
		                                          : Pair{s1_r.Add(s3_i), s1_i.Subtract(s3_r)};
		const std::array<Pair, 4> grouped = transpose(out0, out1, out2, out3);
		store2(dst, grouped[0]);
		store2(dst + 8, grouped[1]);
		store2(dst + 16, grouped[2]);
		store2(dst + 24, grouped[3]);
		dst += 32;
	}
}

/// @brief A later stage of radix 4, four butterflies per pass, rotating inputs 1–3 by the twiddle rows at @p tw
///        (forward) or by their conjugates (inverse) first. @p out may be @p in: each pass reads before it writes.
///        Out of line: the stage loop calls it once per group, and inlined into the transform its loop runs short of
///        registers and spills.
template <Direction D>
[[gnu::noinline]] inline void radix4x4_twiddled(Complex* out, const Complex* in, const Complex* tw, int32_t src_stride,
                                                int32_t dst_stride, int32_t mstride) {
	const auto* src = words(in);
	int32_t* dst = words(out);
	const auto* twp = words(tw);
	const int32_t src_step = src_stride * 2;
	const int32_t dst_step = dst_stride * 2;
	const int32_t tw_step = mstride * 2;
	for (int32_t m = 0; m < mstride; m += 4) {
		const Pair in0 = load2(src);
		const Pair in1 = load2(src + src_step);
		const Pair in2 = load2(src + (2 * src_step));
		const Pair in3 = load2(src + (3 * src_step));
		src += 8;
		const Pair tw0 = load2(twp);
		const Pair tw1 = load2(twp + tw_step);
		const Pair tw2 = load2(twp + (2 * tw_step));
		twp += 8;
		V s1_r = in1.r.MultiplyFixedQMax(tw0.r);
		V s1_i = in1.i.MultiplyFixedQMax(tw0.r);
		V s2_r = in2.r.MultiplyFixedQMax(tw1.r);
		V s2_i = in2.i.MultiplyFixedQMax(tw1.r);
		V s3_r = in3.r.MultiplyFixedQMax(tw2.r);
		V s3_i = in3.i.MultiplyFixedQMax(tw2.r);
		const V tmp0 = in1.i.MultiplyFixedQMax(tw0.i);
		const V tmp1 = in1.r.MultiplyFixedQMax(tw0.i);
		const V tmp2 = in2.i.MultiplyFixedQMax(tw1.i);
		const V tmp3 = in2.r.MultiplyFixedQMax(tw1.i);
		const V tmp4 = in3.i.MultiplyFixedQMax(tw2.i);
		const V tmp5 = in3.r.MultiplyFixedQMax(tw2.i);
		if constexpr (D == Direction::FORWARD) { // rotate by the twiddle
			s1_r = s1_r.Subtract(tmp0);
			s1_i = s1_i.Add(tmp1);
			s2_r = s2_r.Subtract(tmp2);
			s2_i = s2_i.Add(tmp3);
			s3_r = s3_r.Subtract(tmp4);
			s3_i = s3_i.Add(tmp5);
		}
		else { // rotate by the twiddle's conjugate
			s1_r = s1_r.Add(tmp0);
			s1_i = s1_i.Subtract(tmp1);
			s2_r = s2_r.Add(tmp2);
			s2_i = s2_i.Subtract(tmp3);
			s3_r = s3_r.Add(tmp4);
			s3_i = s3_i.Subtract(tmp5);
		}
		const V s4_r = in0.r.Add(s2_r);
		const V s4_i = in0.i.Add(s2_i);
		const V s5_r = in0.r.Subtract(s2_r);
		const V s5_i = in0.i.Subtract(s2_i);
		const V s6_r = s1_r.Add(s3_r);
		const V s6_i = s1_i.Add(s3_i);
		const V s7_r = s1_r.Subtract(s3_r);
		const V s7_i = s1_i.Subtract(s3_i);
		const Pair out2{s4_r.Subtract(s6_r), s4_i.Subtract(s6_i)};
		const Pair out0{s4_r.Add(s6_r), s4_i.Add(s6_i)};
		const Pair out1 = D == Direction::FORWARD ? Pair{s5_r.Add(s7_i), s5_i.Subtract(s7_r)}
		                                          : Pair{s5_r.Subtract(s7_i), s5_i.Add(s7_r)};
		const Pair out3 = D == Direction::FORWARD ? Pair{s5_r.Subtract(s7_i), s5_i.Add(s7_r)}
		                                          : Pair{s5_r.Add(s7_i), s5_i.Subtract(s7_r)};
		store2(dst, out0);
		store2(dst + dst_step, out1);
		store2(dst + (2 * dst_step), out2);
		store2(dst + (3 * dst_step), out3);
		dst += 8;
	}
}

/// @brief The complex transform of length factors' product: a first stage of radix 8 or 4, then radix-4 stages,
///        ping-ponging between @p out and @p buffer, the last stage writing @p out.
template <Direction D>
[[gnu::always_inline]] inline void mixed_radix(Complex* out, const Complex* in, std::span<const int32_t> factors,
                                               const Complex* twiddles, Complex* buffer) {
	int32_t stage_count = factors[0];
	int32_t fstride = factors[1];
	int32_t mstride = factors[(stage_count * 2) - 1];
	int32_t n = factors[stage_count * 2];
	Complex* const last_out = out;
	const Complex* tw = twiddles;

	if (n == 8) {
		n = fstride * 2;
		radix8x4<D>(out, in, fstride);
	}
	else {
		radix4x4_first<D>(out, in, fstride);
		n = fstride;
	}
	fstride /= 4;
	stage_count--;
	std::swap(buffer, out);

	for (; stage_count > 1; stage_count--) {
		const Complex* stage_in = buffer;
		for (int32_t f = 0; f < fstride; f++) {
			radix4x4_twiddled<D>(&out[(f * mstride) * 4], stage_in, tw, n, mstride, mstride);
			stage_in += mstride;
		}
		tw += mstride * 3;
		mstride *= 4;
		std::swap(buffer, out);
		fstride /= 4;
	}

	if (stage_count != 0) {
		const Complex* stage_in = buffer;
		Complex* stage_out = last_out;
		for (int32_t f = 0; f < fstride; f++) {
			radix4x4_twiddled<D>(stage_out, stage_in, tw, n, n, mstride);
			stage_in += mstride;
			stage_out += mstride;
		}
	}
}

/// @brief The forward real split: from the half-length complex transform @p src (ncfft values) to the real
///        transform's ncfft + 1 bins in @p dst, halving each bin but the two ends. ncfft ≥ 8.
[[gnu::always_inline]] inline void split_forward(Complex* dst, const Complex* src, const Complex* tw, int32_t ncfft) {
	const int32_t count = ncfft / 2;
	const Complex tdc = src[0];
	dst[0].r = wrapping_add(tdc.r, tdc.i);
	dst[ncfft].r = wrapping_sub(tdc.r, tdc.i);
	dst[ncfft].i = 0;
	dst[0].i = 0;

	const auto* low_src = words(src + 1);
	const auto* high_src = words(src + (ncfft - 4));
	const auto* twp = words(tw);
	int32_t* low_dst = words(dst + 1);
	int32_t* high_dst = words(dst + (ncfft - 4));
	// Each pass takes bins k…k+3 and their mirrors ncfft−k−3…ncfft−k; the last pass writes the middle bin twice, and
	// the mirror's store, made second, is the one kept.
	for (int32_t k = 1; k <= count; k += 4) {
		const Pair fpk = load2(low_src);
		low_src += 8;
		const Pair fpnk = load2(high_src);
		high_src -= 8;
		const Pair twk = load2(twp);
		twp += 8;
		const V fpnk_r = fpnk.r.Reverse();
		const V fpnk_i = fpnk.i.Reverse().Negate();
		const V f1k_r = fpk.r.Add(fpnk_r);
		const V f1k_i = fpk.i.Add(fpnk_i);
		const V f2k_r = fpk.r.Subtract(fpnk_r);
		const V f2k_i = fpk.i.Subtract(fpnk_i);
		const V tmp0 = f2k_r.MultiplyFixedQMax(twk.r);
		const V tmp1 = f2k_i.MultiplyFixedQMax(twk.i);
		const V tmp2 = f2k_r.MultiplyFixedQMax(twk.i);
		const V tmp3 = f2k_i.MultiplyFixedQMax(twk.r);
		const V tw_r = tmp0.Subtract(tmp1);
		const V tw_i = tmp2.Add(tmp3);
		const V dst2_r = f1k_r.SubtractHalve(tw_r);
		const V dst2_i = tw_i.SubtractHalve(f1k_i);
		store2(low_dst, {f1k_r.AddHalve(tw_r), f1k_i.AddHalve(tw_i)});
		low_dst += 8;
		store2(high_dst, {dst2_r.Reverse(), dst2_i.Reverse()});
		high_dst -= 8;
	}
}

/// @brief The inverse real split: from the real transform's ncfft + 1 bins in @p src to the half-length complex
///        transform's input in @p dst (ncfft values).
[[gnu::always_inline]] inline void split_inverse(Complex* dst, const Complex* src, const Complex* tw, int32_t ncfft) {
	const int32_t count = ncfft / 2;
	dst[0].r = wrapping_add(src[0].r, src[ncfft].r);
	dst[0].i = wrapping_sub(src[0].r, src[ncfft].r);

	if (count >= 4) {
		const auto* low_src = words(src + 1);
		const auto* high_src = words(src + (ncfft - 4));
		const auto* twp = words(tw);
		int32_t* low_dst = words(dst + 1);
		int32_t* high_dst = words(dst + (ncfft - 4));
		// As the forward split: the middle bin's second store, the mirror's, is the one kept.
		for (int32_t k = 1; k <= count; k += 4) {
			const Pair fk = load2(low_src);
			low_src += 8;
			const Pair fnkc = load2(high_src);
			high_src -= 8;
			const Pair twk = load2(twp);
			twp += 8;
			const V fnkc_r = fnkc.r.Reverse();
			const V fnkc_i = fnkc.i.Reverse().Negate();
			const V fek_r = fk.r.Add(fnkc_r);
			const V fek_i = fk.i.Add(fnkc_i);
			const V tmp0 = fk.r.Subtract(fnkc_r);
			const V tmp1 = fk.i.Subtract(fnkc_i);
			V fok_r = tmp0.MultiplyFixedQMax(twk.r);
			V fok_i = tmp1.MultiplyFixedQMax(twk.r);
			const V tmp2 = tmp1.MultiplyFixedQMax(twk.i);
			const V tmp3 = tmp0.MultiplyFixedQMax(twk.i);
			fok_r = fok_r.Add(tmp2);
			fok_i = fok_i.Subtract(tmp3);
			const V dst2_r = fek_r.Subtract(fok_r);
			const V dst2_i = fok_i.Subtract(fek_i);
			store2(low_dst, {fek_r.Add(fok_r), fek_i.Add(fok_i)});
			low_dst += 8;
			store2(high_dst, {dst2_r.Reverse(), dst2_i.Reverse()});
			high_dst -= 8;
		}
		return;
	}

	for (int32_t k = 1; k <= count; k++) {
		const Complex fk = src[k];
		const Complex fnkc{src[ncfft - k].r, wrapping_sub(0, src[ncfft - k].i)};
		const int32_t fek_r = wrapping_add(fk.r, fnkc.r);
		const int32_t fek_i = wrapping_add(fk.i, fnkc.i);
		const int32_t tmp_r = wrapping_sub(fk.r, fnkc.r);
		const int32_t tmp_i = wrapping_sub(fk.i, fnkc.i);
		const int32_t fok_r = doubled(wrapping_add(high_product(tmp_r, tw[k - 1].r), high_product(tmp_i, tw[k - 1].i)));
		const int32_t fok_i = doubled(wrapping_sub(high_product(tmp_i, tw[k - 1].r), high_product(tmp_r, tw[k - 1].i)));
		dst[k] = {wrapping_add(fek_r, fok_r), wrapping_add(fek_i, fok_i)};
		dst[ncfft - k] = {wrapping_sub(fek_r, fok_r), wrapping_sub(fok_i, fek_i)};
	}
}

} // namespace deluge::dsp::fixed::fft::detail
