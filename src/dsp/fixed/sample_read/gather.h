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

#include "dsp/core/fixed_point.h"
#include "dsp/core/fixed_point_neon.h"
#include "dsp/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp/fixed/sample_read/stage.h"

#include <arm_neon.h>
#include <array>
#include <cstdint>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief The sample read's interpolation over staging (stage.h): each output's taps read straight from memory at its
/// position, four outputs reduced together, instead of a window slid and fed one output at a time.
///
/// A position is the staging slot of an output's newest tap and the phase between it and the next. A step is the
/// sliding window's: the phase on by the increment, the slot down by the phase's whole part. Staging is newest first,
/// so the sixteen slots from a position are the window the slide would hold there, in the windowed-sinc table's order;
/// a step longer than the window passes over frames it never reads, as the slide's jump did. The dot products are
/// wrapping sums of exact Q15 × Q15 products, so their order is free and four outputs reduce at once.

/// @brief A gather's position: the staging slot of the newest tap (play moves to lower slots), and the Q24 phase.
struct GatherPos {
	int32_t newest;
	uint32_t phase;
};

namespace detail {

inline constexpr uint32_t kPhaseFraction = (1u << 24) - 1;

/// @brief The windowed-sinc kernel row for @p phase, blended toward the next row per tap: the interpolator's blend.
[[gnu::always_inline]] inline void sinc_kernel(const int16_t* rows, uint32_t phase, int16x8_t& k0, int16x8_t& k1) {
	// The phase's top four fraction bits pick the row, the next fifteen blend toward the next row: the interpolator's
	// bit fields of the Q24 word
	const int16_t* row = rows + ((phase >> 20) * kSincKernelTaps);
	const auto blend = static_cast<int16_t>((phase >> 5) & 0x7FFF);
	const int16x8_t r0 = vld1q_s16(row);
	const int16x8_t r1 = vld1q_s16(row + 8);
	k0 = vaddq_s16(r0, vqdmulhq_n_s16(vsubq_s16(vld1q_s16(row + kSincKernelTaps), r0), blend));
	k1 = vaddq_s16(r1, vqdmulhq_n_s16(vsubq_s16(vld1q_s16(row + kSincKernelTaps + 8), r1), blend));
}

/// @brief The dot of a blended kernel with the sixteen taps from @p taps, folded to two lanes: wrapping sums of the
/// exact products, whose order is free.
[[gnu::always_inline]] inline int32x2_t sinc_dot(int16x8_t k0, int16x8_t k1, const int16_t* taps) {
	const int16x8_t t0 = vld1q_s16(taps);
	const int16x8_t t1 = vld1q_s16(taps + 8);
	int32x4_t a = vmull_s16(vget_low_s16(k0), vget_low_s16(t0));
	a = vmlal_s16(a, vget_high_s16(k0), vget_high_s16(t0));
	a = vmlal_s16(a, vget_low_s16(k1), vget_low_s16(t1));
	a = vmlal_s16(a, vget_high_s16(k1), vget_high_s16(t1));
	return vadd_s32(vget_low_s32(a), vget_high_s32(a));
}

/// @brief Four outputs' folded lanes reduced to one vector, output i in lane i.
[[gnu::always_inline]] inline QVec<31> reduce4(int32x2_t a, int32x2_t b, int32x2_t c, int32x2_t d) {
	// Each output's dot is a Q30 word of the window's Q15 samples, read as the Q31 sample, as interpolate reads it
	return QVec<31>::from_raw(vcombine_s32(vpadd_s32(a, b), vpadd_s32(c, d)));
}

} // namespace detail

/// @brief Gathers @p outputs windowed-sinc outputs from @p s, from @p pos, into @p sink four at a time
/// (`sink.put(l, r, count)`, count below 4 only on the last call; @p r repeats @p l for a mono source).
/// @param kernel The kernel variant (`WindowedSincKernel::variant_for`).
/// @param step_first Whether the first output steps first; a read's first output does not (R3 took its step).
/// @return The position after the last output: its newest slot and phase, with no step after it.
template <int Channels, class Sink>
GatherPos sinc_gather(const Staging& s, GatherPos pos, uint32_t inc, int32_t kernel, int32_t outputs, bool step_first,
                      Sink& sink) {
	const int16_t* rows = kWindowedSincKernel.variant(kernel);
	int32_t newest = pos.newest;
	uint32_t phase = pos.phase;
	// The phase is the Q24 word the read head steps (R3, R8): its whole part is the frames a step passes, so the
	// crossings here are that word's arithmetic
	auto step = [&] [[gnu::always_inline]] {
		phase += inc;
		newest -= static_cast<int32_t>(phase >> 24);
		phase &= detail::kPhaseFraction;
	};
	auto one = [&] [[gnu::always_inline]] (int32x2_t & dl, int32x2_t & dr) {
		int16x8_t k0;
		int16x8_t k1;
		detail::sinc_kernel(rows, phase, k0, k1);
		dl = detail::sinc_dot(k0, k1, s.l.data() + newest);
		if constexpr (Channels == 2) {
			dr = detail::sinc_dot(k0, k1, s.r.data() + newest);
		}
		else {
			dr = dl;
		}
	};
	if (outputs <= 0) {
		return pos;
	}
	if (step_first) {
		step();
	}
	// Whole groups are straight-line code, so one output's loads and multiplies interleave with the next's; each
	// output, then the step to the next, the step after the last not taken
	int32_t done = 0;
	for (; outputs - done >= 4; done += 4) {
		int32x2_t l0;
		int32x2_t l1;
		int32x2_t l2;
		int32x2_t l3;
		int32x2_t r0;
		int32x2_t r1;
		int32x2_t r2;
		int32x2_t r3;
		one(l0, r0);
		step();
		one(l1, r1);
		step();
		one(l2, r2);
		step();
		one(l3, r3);
		sink.put(detail::reduce4(l0, l1, l2, l3), detail::reduce4(r0, r1, r2, r3), 4);
		if (done + 4 == outputs) {
			return {.newest = newest, .phase = phase};
		}
		step();
	}
	// The last one to three outputs
	const int32_t count = outputs - done;
	int32x2_t l0;
	int32x2_t r0;
	int32x2_t l1 = vdup_n_s32(0);
	int32x2_t r1 = vdup_n_s32(0);
	int32x2_t l2 = vdup_n_s32(0);
	int32x2_t r2 = vdup_n_s32(0);
	one(l0, r0);
	if (count > 1) {
		step();
		one(l1, r1);
	}
	if (count > 2) {
		step();
		one(l2, r2);
	}
	sink.put(detail::reduce4(l0, l1, l2, vdup_n_s32(0)), detail::reduce4(r0, r1, r2, vdup_n_s32(0)), count);
	return {.newest = newest, .phase = phase};
}

/// @brief Gathers @p outputs linear outputs from @p s, from @p pos, into @p sink four at a time: each output its
/// previous tap (the slot above its newest) and its newest weighted by the phase, `previous · (32767 − s) + newest · s`
/// with `s = phase >> 9`, integer products read as the Q31 sample (R9). Steps, @p step_first and the result are as
/// sinc_gather's.
template <int Channels, class Sink>
GatherPos linear_gather(const Staging& s, GatherPos pos, uint32_t inc, int32_t outputs, bool step_first, Sink& sink) {
	if (outputs <= 0) {
		return pos;
	}
	int32_t newest = pos.newest;
	uint32_t phase = pos.phase;
	// The phase is the Q24 word the read head steps (R3, R9); the strengths are its bit field, as the scalar read's
	auto step = [&] [[gnu::always_inline]] {
		phase += inc;
		newest -= static_cast<int32_t>(phase >> 24);
		phase &= detail::kPhaseFraction;
	};
	const std::array<uint32_t, 4> offsets{0, inc, 2 * inc, 3 * inc};
	const uint32x4_t phase_offsets = vld1q_u32(offsets.data());
	const uint32x4_t fraction = vdupq_n_u32(detail::kPhaseFraction);
	const int16x4_t full = vdup_n_s16(32767);
	// Four outputs from their newest slots, the phases from the first's (the fraction of a sum is the sum's fraction)
	auto four = [&] [[gnu::always_inline]] (const std::array<int32_t, 4>& at, uint32_t first_phase, int32_t count) {
		const uint32x4_t phases = vandq_u32(vaddq_u32(vdupq_n_u32(first_phase), phase_offsets), fraction);
		const int16x4_t s2 = vreinterpret_s16_u16(vmovn_u32(vshrq_n_u32(phases, 9)));
		const int16x4_t s1 = vsub_s16(full, s2);
		// Each output's pair, newest then previous, is one element-aligned halfword load from its newest slot
		auto weigh = [&] [[gnu::always_inline]] (const int16_t* taps) {
			const int32x2_t a =
			    vtrn_s32(vreinterpret_s32_s16(vld1_s16(taps + at[0])), vreinterpret_s32_s16(vld1_s16(taps + at[1])))
			        .val[0];
			const int32x2_t b =
			    vtrn_s32(vreinterpret_s32_s16(vld1_s16(taps + at[2])), vreinterpret_s32_s16(vld1_s16(taps + at[3])))
			        .val[0];
			const int16x4x2_t pairs = vuzp_s16(vreinterpret_s16_s32(a), vreinterpret_s16_s32(b));
			// The integer products, read as the Q31 sample as the scalar read reads them
			return QVec<31>::from_raw(vmlal_s16(vmull_s16(pairs.val[1], s1), pairs.val[0], s2));
		};
		const QVec<31> l = weigh(s.l.data());
		sink.put(l, Channels == 2 ? weigh(s.r.data()) : l, count);
	};
	if (step_first) {
		step();
	}
	int32_t done = 0;
	while (true) {
		const int32_t count = outputs - done < 4 ? outputs - done : 4;
		const uint32_t first_phase = phase;
		std::array<int32_t, 4> at{newest, newest, newest, newest};
		for (int32_t j = 1; j < count; j++) {
			step();
			at[j] = newest;
		}
		four(at, first_phase, count);
		done += count;
		if (done == outputs) {
			break;
		}
		step();
	}
	return {.newest = newest, .phase = phase};
}

} // namespace deluge::dsp::fixed::sample_read
