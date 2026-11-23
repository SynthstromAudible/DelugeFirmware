/*
 * Copyright © 2016-2026 Synthstrom Audible Limited
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

#include "dsp/core/divide.h"
#include "dsp/core/fixed_point.h"

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @file
/// @brief Window-match metrics for the time-stretch hop search (shared by the sample time-stretcher and the
/// live-input pitch-shifter). Each compares two equal-length sets of crossfade moving-average totals — the running
/// averages of a candidate splice window vs. the reference one — so the search can pick where the two best line up.

/// @brief Total absolute difference between two windows' moving-average totals: `Σ |b[i] − a[i]|`.
/// The hop search minimises this — the smaller it is, the better the two windows match for a click-free splice.
[[nodiscard]] constexpr int32_t totalDifferenceAbs(std::span<const int32_t> a, std::span<const int32_t> b) {
	int32_t total = 0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		int32_t difference = b[i] - a[i];
		total += (difference < 0) ? -difference : difference;
	}
	return total;
}

/// @brief Net signed change between two windows' moving-average totals: `Σ b[i] − Σ a[i]`.
[[nodiscard]] constexpr int32_t totalChange(std::span<const int32_t> a, std::span<const int32_t> b) {
	int32_t total = 0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		total += b[i] - a[i];
	}
	return total;
}

} // namespace deluge::dsp::fixed

namespace deluge::dsp::fixed::timestretch {

/// @brief A crossfade between a stretcher's two heads: its progress from the older head to the newer, and its step per
/// output frame (X1 of docs/dev/time_stretcher_contracts.md).
struct CrossfadeRamp {
	uint32_t progress = 1u << 24; ///< Q24 of 1.0 (2^24 is done); runs unsigned and may step past 2^24.
	uint32_t increment = 0;       ///< Per output frame.

	/// A hop's crossfade over @p frames output frames: 2^24 / frames.
	[[nodiscard]] static constexpr CrossfadeRamp over_hop(int32_t frames) {
		return {.progress = 0, .increment = (1u << 24) / static_cast<uint32_t>(frames)};
	}
	/// A crossfade into a loop's pre-margin: rounded up from 2^24 - 1.
	[[nodiscard]] static constexpr CrossfadeRamp over_loop(int32_t frames) {
		return {.progress = 0,
		        .increment = static_cast<uint32_t>((1u << 24) - 1 + frames) / static_cast<uint32_t>(frames)};
	}
	/// A fudged crossfade at a loop end: rounded up from 2^24, so it is one more than over_loop() where @p frames
	/// divides 2^24 (X1's quirk).
	[[nodiscard]] static constexpr CrossfadeRamp over_fudge(int32_t frames) {
		return {.progress = 0, .increment = static_cast<uint32_t>((1u << 24) + frames) / static_cast<uint32_t>(frames)};
	}
	/// A crossfade out of a cache tail of @p frames (at most 255): 2^24 over the frames' low 16 bits, plus one.
	[[nodiscard]] static constexpr CrossfadeRamp over_cache_tail(int32_t frames) {
		return {.progress = 0, .increment = (1u << 24) / static_cast<uint16_t>(frames) + 1};
	}
	[[nodiscard]] constexpr bool done() const { return progress >= (1u << 24); }
};

/// @brief Each head's level over one segment of output: where it starts and its step per frame.
struct HeadGains {
	Q<31> newer;
	Q<31> newer_step;
	Q<31> older;
	Q<31> older_step;
};

/// The progress is an unsigned word read as Q24, and the step a word quotient (divide_by_small), so three values cross
/// from words.
/// @brief The heads' gains over the next @p frames of output at source level @p level stepping by @p level_step, the
/// ramp moved on by them (X2). Linear: the newer head at the ramp's progress, the older at the rest of full scale.
[[nodiscard]] inline HeadGains segment_gains(CrossfadeRamp& ramp, int32_t frames, Q<31> level, Q<31> level_step) {
	// The progress word read as signed Q24 (it runs 0 to 2^24, so the bits agree) as the newer head's Q31 level
	const auto newer_now = as_wrap<Q<31>>(Q<24>::from_raw(static_cast<int32_t>(ramp.progress)));
	const Q<31> older_now = sub_wrap(Q<31>::max(), newer_now);
	ramp.progress += ramp.increment * static_cast<uint32_t>(frames);
	// Where the progress will be, held under 1.0 before its move to Q31: the step can carry it past 2^24
	const auto newer_after = as_clamp_then_shift<Q<31>>(Q<24>::from_raw(static_cast<int32_t>(ramp.progress)));
	const Q<31> newer_step =
	    Q<31>::from_raw(deluge::dsp::divide_by_small(sub_wrap(newer_after, newer_now).raw(), frames));
	const auto change = mul_as_wrap<Q<31>>(level, newer_step);
	return {
	    .newer = mul_as_wrap<Q<31>>(level, newer_now),
	    .newer_step = add_wrap(level_step, change),
	    .older = mul_as_wrap<Q<31>>(level, older_now),
	    .older_step = sub_wrap(level_step, change),
	};
}

} // namespace deluge::dsp::fixed::timestretch
