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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/exp_curve.h"
#include "util/fixedpoint.h"

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
/// position passes one frame only if the rate rose mid-grab, and is clamped there. The output is ≪ 2. Each
/// interpolation's two products, rounded, sum wrapping as the target's `add` does.
///
/// **Events.** @ref start zeroes the last input, the grab and the low position, as the effect does each time it is
/// switched on; the last grab and the high position carry over.
struct SampleRateReducer {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr uint32_t kOne = 4194304;       ///< One frame in 22-bit fixed point.
	static constexpr uint32_t kBelowOne = kOne - 1; ///< The largest position below one frame.
	static constexpr int kPositionToQ31Shift = 9;   ///< A 22-bit fraction ≪ this is Q31.
	static constexpr int kOutputShift = 2;          ///< The up-conversion's output shift.

	/// @brief The param, and the mask the bit depth puts on each grab.
	struct Settings {
		q31 sample_rate_reduction = std::numeric_limits<q31>::min(); ///< The param, Q31; on above its minimum.
		uint32_t grab_mask = 0xFFFFFFFFu;                            ///< BitDepth::mask at BitDepth::kGrabMaskBase.
	};
	/// @brief The two rates and the grab mask.
	struct Coefficients {
		q31 low_increment = 0;            ///< Per frame, 22-bit.
		q31 high_increment = 0;           ///< Per frame: the low increment's reciprocal ≪ 6.
		uint32_t grab_mask = 0xFFFFFFFFu; ///< The bits a grab keeps.
	};
	/// @brief The positions and the frames the interpolations read.
	struct State {
		uint32_t low_pos = 0;        ///< The down-conversion's position, 22-bit.
		uint32_t high_pos = 0;       ///< The up-conversion's position, 22-bit.
		StereoSample last_sample{};  ///< The last input frame.
		StereoSample grabbed{};      ///< The latest grab.
		StereoSample last_grabbed{}; ///< The grab before it.
	};

	/// @brief Whether @p sample_rate_reduction reduces the rate.
	[[nodiscard]] static constexpr bool on(q31 sample_rate_reduction) {
		return sample_rate_reduction != std::numeric_limits<q31>::min();
	}

	/// @brief The rates for @p s; the param must be on.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		const uint32_t preset = static_cast<uint32_t>(s.sample_rate_reduction) + 2147483648u;
		const q31 low = exp_curve(static_cast<q31>(kOne), static_cast<q31>(preset >> 3));
		const auto high = static_cast<q31>((0xFFFFFFFFu / static_cast<uint32_t>(low >> 6)) << 6);
		return {.low_increment = low, .high_increment = high, .grab_mask = s.grab_mask};
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
			const auto toward_this = static_cast<q31>(s.low_pos);
			const q31 toward_last = static_cast<q31>(kBelowOne) - toward_this;
			s.last_grabbed = s.grabbed;
			s.grabbed = {
			    .l = grab(s.last_sample.l, x.l, toward_last, toward_this, c.grab_mask),
			    .r = grab(s.last_sample.r, x.r, toward_last, toward_this, c.grab_mask),
			};
			s.low_pos += static_cast<uint32_t>(c.low_increment);
			s.high_pos = static_cast<uint32_t>(
			    multiply_32x32_rshift32_rounded(static_cast<q31>(s.low_pos & kBelowOne), c.high_increment << 8) << 2);
		}
		s.low_pos -= kOne;
		s.last_sample = x;

		const auto toward_grabbed = static_cast<q31>(std::min(s.high_pos, kBelowOne));
		const q31 toward_last_grabbed = static_cast<q31>(kBelowOne) - toward_grabbed;
		const StereoSample out{
		    .l = between(s.last_grabbed.l, s.grabbed.l, toward_last_grabbed, toward_grabbed) << kOutputShift,
		    .r = between(s.last_grabbed.r, s.grabbed.r, toward_last_grabbed, toward_grabbed) << kOutputShift,
		};
		s.high_pos += static_cast<uint32_t>(c.high_increment);
		return out;
	}

private:
	/// @brief @p a × @p toward_a + @p b × @p toward_b, the weights 22-bit, each product rounded, the sum wrapping.
	[[nodiscard]] [[gnu::always_inline]] static q31 between(q31 a, q31 b, q31 toward_a, q31 toward_b) {
		return wrapping_add(multiply_32x32_rshift32_rounded(a, toward_a << kPositionToQ31Shift),
		                    multiply_32x32_rshift32_rounded(b, toward_b << kPositionToQ31Shift));
	}

	/// @brief A grab: @ref between the last input and this one, masked by @p mask.
	[[nodiscard]] [[gnu::always_inline]] static q31 grab(q31 last, q31 now, q31 toward_last, q31 toward_now,
	                                                     uint32_t mask) {
		return static_cast<q31>(static_cast<uint32_t>(between(last, now, toward_last, toward_now)) & mask);
	}
};

static_assert(processor_kernel<SampleRateReducer>);
static_assert(!std::is_polymorphic_v<SampleRateReducer>);

} // namespace deluge::dsp::fixed
