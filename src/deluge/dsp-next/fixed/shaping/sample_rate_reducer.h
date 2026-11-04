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
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/curves/exp_curve.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>

/// @file
/// @brief SampleRateReducer: the bitcrush's sample-rate reduction over stereo frames, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief Grabs frames at a lower rate, each interpolated between the last input and this one and masked, and
/// interpolates back up between the last two grabs.
/// @details Positions are 22-bit fixed point (@ref kOne is one frame), which keeps the low rates from clipping. The
/// low rate's increment is exp_curve(kOne, preset ≫ 3), preset = param + 2^31; the high rate's is its reciprocal,
/// (2^32 − 1) ÷ (increment ≫ 6) ≪ 6. Each grab re-syncs the up-conversion to where it fell within the frame. The up
/// position passes one frame only if the rate rose mid-grab, and is clamped there. Each interpolation's two products,
/// rounded, sum wrapping as the target's `add` does.
///
/// **Formats.** A position's fraction of a frame is Q22, the rates Q22 frames per frame, and an interpolation's
/// weight that fraction reformatted to Q31. Interpolating Q<F> values gives Q<F − 1>: the grabs are Q30 of the input,
/// and the up-conversion Q29, reformatted to Q31 for the output. That reformat cannot wrap: a blend's two weights sum
/// to (2^22 − 1) · 2^9, just under one, so a grab stays inside Q30's range and the up-conversion inside Q29's.
///
/// **Events.** @ref start zeroes the last input, the grab and the low position, as the effect does each time it is
/// switched on; the last grab and the high position carry over.
struct SampleRateReducer {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr uint32_t kOne = 4194304;             ///< One frame in 22-bit fixed point.
	static constexpr uint32_t kBelowOne = kOne - 1;       ///< The largest position below one frame.
	static constexpr uint32_t kParamOffset = 2147483648u; ///< 2^31: lifts the param to [0, 2^32) for its preset.
	static constexpr int kPresetToAdjustShift = 3;        ///< The preset's shift into exp_curve's adjustment.
	static constexpr int kReciprocalShift =
	    6; ///< The low increment's shift before its reciprocal, and the result's after.
	static constexpr uint32_t kAllBits =
	    0xFFFFFFFFu; ///< The mask that keeps every bit, and 2^32 − 1 for the reciprocal.

	/// @brief The param, and the mask the bit depth puts on each grab.
	struct Settings {
		Q<31> sample_rate_reduction = Q<31>::min(); ///< The param; on above its minimum.
		uint32_t grab_mask = kAllBits;              ///< BitDepth::mask at BitDepth::kGrabMaskBase.
	};
	/// @brief The two rates and the grab mask.
	struct Coefficients {
		Q<22> low_increment{};         ///< Per frame.
		Q<22> high_increment{};        ///< Per frame: the low increment's reciprocal ≪ 6.
		uint32_t grab_mask = kAllBits; ///< The bits a grab keeps.
	};
	/// @brief The positions and the frames the interpolations read.
	struct State {
		uint32_t low_pos = 0;        ///< The down-conversion's position, 22-bit.
		uint32_t high_pos = 0;       ///< The up-conversion's position, 22-bit.
		StereoSample last_sample{};  ///< The last input frame.
		StereoSample grabbed{};      ///< The latest grab, Q30.
		StereoSample last_grabbed{}; ///< The grab before it, Q30.
	};

	/// @brief Whether @p sample_rate_reduction reduces the rate.
	[[nodiscard]] static constexpr bool on(Q<31> sample_rate_reduction) {
		return sample_rate_reduction != Q<31>::min();
	}

	/// @brief The rates for @p s; the param must be on.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		const uint32_t preset = static_cast<uint32_t>(s.sample_rate_reduction.raw()) + kParamOffset;
		// exp_curve scales one frame, so the rate keeps its Q22
		const int32_t low = exp_curve(static_cast<int32_t>(kOne), static_cast<int32_t>(preset >> kPresetToAdjustShift));
		const auto high =
		    static_cast<int32_t>((kAllBits / static_cast<uint32_t>(low >> kReciprocalShift)) << kReciprocalShift);
		return {
		    .low_increment = Q<22>::from_raw(low), .high_increment = Q<22>::from_raw(high), .grab_mask = s.grab_mask};
	}

	/// @brief The effect switched on: zero the last input, the grab and the low position.
	static void start(State& s) {
		s.last_sample = {};
		s.grabbed = {};
		s.low_pos = 0;
	}

	/// @brief One frame: a grab when the low position is within it, then the up-conversion between the last two grabs.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		if (s.low_pos < kOne) {
			const auto toward_this = Q<22>::from_raw(static_cast<int32_t>(s.low_pos));
			const Q<22> toward_last = sub_wrap(Q<22>::from_raw(kBelowOne), toward_this);
			s.last_grabbed = s.grabbed;
			s.grabbed = {
			    .l = grab(s.last_sample.l, x.l, toward_last, toward_this, c.grab_mask),
			    .r = grab(s.last_sample.r, x.r, toward_last, toward_this, c.grab_mask),
			};
			s.low_pos += static_cast<uint32_t>(c.low_increment.raw());
			// Where in the frame the grab fell, in the high rate's frames
			const Q<20> resync =
			    mul_r(Q<22>::from_raw(static_cast<int32_t>(s.low_pos & kBelowOne)), as_wrap<Q<30>>(c.high_increment));
			s.high_pos = static_cast<uint32_t>(as_wrap<Q<22>>(resync).raw());
		}
		s.low_pos -= kOne;
		s.last_sample = x;

		const auto toward_grabbed = Q<22>::from_raw(static_cast<int32_t>(std::min(s.high_pos, kBelowOne)));
		const Q<22> toward_last_grabbed = sub_wrap(Q<22>::from_raw(kBelowOne), toward_grabbed);
		const StereoSample out{
		    .l = as_wrap<Q<31>>(between(Q<30>::from_raw(s.last_grabbed.l), Q<30>::from_raw(s.grabbed.l),
		                                toward_last_grabbed, toward_grabbed))
		             .raw(),
		    .r = as_wrap<Q<31>>(between(Q<30>::from_raw(s.last_grabbed.r), Q<30>::from_raw(s.grabbed.r),
		                                toward_last_grabbed, toward_grabbed))
		             .raw(),
		};
		s.high_pos += static_cast<uint32_t>(c.high_increment.raw());
		return out;
	}

private:
	/// @brief @p a × @p toward_a + @p b × @p toward_b, the weights Q22 reformatted to Q31, each product rounded, the
	/// sum wrapping: Q<F − 1>.
	template <int F>
	[[nodiscard]] [[gnu::always_inline]] static Q<F - 1> between(Q<F> a, Q<F> b, Q<22> toward_a, Q<22> toward_b) {
		return add_wrap(mul_r(a, as_wrap<Q<31>>(toward_a)), mul_r(b, as_wrap<Q<31>>(toward_b)));
	}

	/// @brief A grab: @ref between the last input and this one, Q30, masked by @p mask.
	[[nodiscard]] [[gnu::always_inline]] static q31_t grab(q31_t last, q31_t now, Q<22> toward_last, Q<22> toward_now,
	                                                       uint32_t mask) {
		const Q<30> grabbed = between(Q<31>::from_raw(last), Q<31>::from_raw(now), toward_last, toward_now);
		return static_cast<q31_t>(static_cast<uint32_t>(grabbed.raw()) & mask);
	}
};

static_assert(processor_kernel<SampleRateReducer>);
static_assert(!std::is_polymorphic_v<SampleRateReducer>);

} // namespace deluge::dsp::fixed
