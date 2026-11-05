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

#include "definitions_cxx.hpp" // kDifferenceLPFPoles
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <array>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The live input's slope follower and the percussiveness read from it: the pitch shifter's onset measure.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief The input's slope, followed: the magnitude of each sample's change, smoothed by low-passes in series.
/// @details A processor kernel of Q31 samples. Each step takes the input folded to one channel (a quarter of each
/// channel, summed) and returns the smoothed slope. Each low-pass moves its memory a 1/512 step of the distance to its
/// input, rounded: the high word of the distance times 2^23, which the format types as Q30 but which is added to the
/// Q31 memory as a word (the step's arithmetic as the pitch shifter tuned it).
struct SlopeFollower {
	using value_type = Q<31>; ///< The folded input; the slope out.
	using step_type = Q<31>;  ///< One sample per step.

	/// The low-passes in series.
	static constexpr std::size_t kPoles = kDifferenceLPFPoles;
	/// Each low-pass's step toward its input: the rounded high word of the distance times this, 2^23.
	static constexpr Q<31> kStep = q<31>(0x1p-8);

	struct Settings {};
	struct Coefficients {};

	/// @brief The follower's memory.
	struct State {
		Q<31> last{};                      ///< The previous folded input.
		std::array<Q<31>, kPoles> poles{}; ///< Each low-pass's memory; the last is the slope.
	};

	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& /*s*/) {
		return {};
	}

	/// @brief The slope after the step before: the last low-pass's memory.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Q<31> slope(const State& s) { return s.poles.back(); }

	/// @brief One folded input sample, @p x.
	/// @return The slope after it.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, State& s,
	                                                       Q<31> x) {
		// The change wraps: a stereo fold spans ±2^30, so a full-scale swing is 2^31
		Q<31> follow = sub_wrap(x, s.last);
		s.last = x;
		if (follow < Q<31>{}) {
			follow = neg_wrap(follow);
		}
		for (Q<31>& pole : s.poles) {
			// The Q30 product's word, added as a Q31 word: a 1/512 step
			pole = add_wrap(pole, reinterpret_bits<Q<31>>(mul_r(sub_wrap(follow, pole), kStep)));
			follow = pole;
		}
		return follow;
	}
};

static_assert(processor_kernel<SlopeFollower>);
static_assert(!std::is_polymorphic_v<SlopeFollower>);

/// @brief The gain on the slope's change before it is divided by the slope: 2^18.
inline constexpr uint64_t kPercussivenessGain = 262144;

/// @brief How percussive the input is where the slope moved from @p previous to @p slope: 0 to 63.
/// @details The change's magnitude times 2^18 over the slope, halved, saturated by `tanh<23>`, whose output (shifted
/// down by 25 bits) is what makes the measure fit a byte. 0 where the slope is 0. The change is taken over one frame of
/// the smoothed slope, though the measure is kept only every 128 frames (quirk Q3,
/// docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md). The arithmetic is on the words, as the measure
/// was tuned: the change and the slope widen to 64 bits as unsigned words, and the quotient narrows to 32 and its tanh
/// to 8.
[[nodiscard]] inline uint8_t percussiveness(Q<31> slope, Q<31> previous) {
	Q<31> change = sub_wrap(slope, previous);
	if (change < Q<31>{}) {
		change = neg_wrap(change);
	}
	if (slope == Q<31>{}) {
		return 0;
	}
	// Words: the measure divides the change by the slope as integers
	const auto ratio = static_cast<int32_t>(
	    ((static_cast<uint64_t>(change.raw()) * kPercussivenessGain) / static_cast<uint64_t>(slope.raw())) >> 1);
	return static_cast<uint8_t>(tanh<23>(Q<31>::from_raw(ratio)).raw()); // A byte of the shifted-down tanh's word
}

} // namespace deluge::dsp::fixed::pitch_shift
