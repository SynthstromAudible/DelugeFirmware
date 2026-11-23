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
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/frame_source.h"
#include "dsp/fixed/timestretch/crossfade.h"
#include "dsp/fixed/timestretch/hop_tuning.h"
#include "dsp/fixed/timestretch/perc_analysis.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// The planner's arithmetic is the legacy's on words (widths, frames and Q24 ratios multiplied and shifted in 64
/// bits), so its typed ratio, increment and shape cross to words where B1 to B7 compute.
/// @brief The time stretcher's hop planner: where its next head starts, and how long that hop and its crossfade are
/// (B1 to B8 of docs/dev/time_stretcher_contracts.md). The ratio is read unsigned throughout and a hop is never
/// planned under 2 frames (decision 3).

/// How near a loop's end a hop crossfades into the loop's pre-margin, in output frames.
inline constexpr int32_t kLoopCrossfadeReach = 100;

/// @brief The perc bytes a search may read: bytes[0] is pixel @ref origin, and the search stays within
/// [earliest, latest] (inclusive, in the direction's order).
struct PercWindow {
	std::span<const uint8_t> bytes;
	int32_t origin;
	int32_t earliest;
	int32_t latest;
};

/// @brief What the planner needs of a loop the stretcher crossfades itself, when it has a pre-margin.
struct LoopAhead {
	int32_t loop_end_frame{};
	int32_t loop_start_frame{};
	bool cache_in_use{}; ///< A cache is being written or read: the new head cannot move to align.
};

/// @brief One hop's facts.
struct HopRequest {
	int64_t position_frames{}; ///< The source position's whole frame.
	Ratio ratio{};
	Increment increment{};
	uint64_t combined{}; ///< The increment the source moves by per output frame, ratio and pitch together.
	Direction direction{Direction::FORWARD};
	int32_t length_frames{}; ///< The waveform's.
	HopShape shape{};        ///< After the jitter (B1).
	std::optional<LoopAhead> loop;
};

/// @brief Where the next head starts and for how long.
struct HopPlan {
	enum class Kind : uint8_t { PAST_END, HEAD, LOOP_CROSSFADE } kind{};
	int32_t start_frame{}; ///< Before alignment.
	int32_t hop_frames{};
	int32_t crossfade_frames{};
	CrossfadeRamp ramp{};
	bool align{};
	int32_t max_hop_frames{}; ///< The ceiling a near loop end set, else INT32_MAX.
};

struct Widths {
	int32_t min{};
	int32_t max{};
	int32_t best{};
};

struct HopLengths {
	int32_t hop_frames{};
	int32_t crossfade_frames{};
	CrossfadeRamp ramp{};
};

struct LoopPlan {
	enum class Kind : uint8_t { NONE, CROSSFADE, CEILING } kind{};
	int32_t start_frame{};
	int32_t crossfade_frames{};
	int32_t hop_frames{};
	int32_t ceiling{};
	bool align{};
};

/// @brief @p shape with its minimum width moved at random by one draw of @p noise, up to its jitter share of itself
/// (B1). The maximum is not jittered.
[[nodiscard]] inline HopShape jittered(HopShape shape, Cong& noise) {
	const auto fraction = mul(Q<31>::from_raw(static_cast<int32_t>(noise.next())),
	                          Q<31>::from_raw(shape.jitter_share.raw() << 8)); // the share moved up 8, Q30
	shape.min_width += scale_shl_wrap<2>(mul(Q<31>::from_raw(shape.min_width), fraction)).raw();
	return shape;
}

/// @brief The widths repitched by @p increment, and the default best between them (B2).
[[nodiscard]] constexpr Widths repitched(const HopShape& shape, Increment increment) {
	const auto scale = [&](int32_t w) {
		return static_cast<int32_t>((uint64_t{static_cast<uint32_t>(w)} * increment.raw()) >> 24);
	};
	const int32_t min = scale(shape.min_width);
	const int32_t max = scale(shape.max_width);
	return {.min = min, .max = max, .best = (min + max) >> 1};
}

/// @brief The width whose beam over @p perc is most percussive on average (B3): widths from the minimum below the
/// maximum in steps of a pixel, each beam reaching back and forward from the position by the ratio, a running total
/// kept as the beam moves; ties go to the wider. Stops where a beam would leave the window.
[[nodiscard]] inline int32_t best_width(const PercWindow& perc, Widths widths, Ratio ratio, int64_t position_frames,
                                        Direction direction) {
	const auto d = static_cast<int32_t>(direction);
	const int64_t below = static_cast<int64_t>(ratio.raw()) - (1 << 24);
	const uint64_t above = uint64_t{ratio.raw()} + (1u << 24);
	const auto top = static_cast<int32_t>(position_frames >> kPercPixelShift);
	auto byte_at = [&](int32_t pixel) { return static_cast<int32_t>(perc.bytes[pixel - perc.origin]); };
	int32_t back_searched = top;
	int32_t front_searched = top;
	int32_t total = 0;
	int32_t best_total = 0;
	int32_t best_pixels = 1;
	int32_t best = widths.best;
	for (auto w = static_cast<uint32_t>(widths.min); w < static_cast<uint32_t>(widths.max);
	     w += 1u << kPercPixelShift) {
		const int32_t back = top + static_cast<int32_t>((static_cast<int64_t>(w) * below) >> 32) * d;
		const int32_t front = top + static_cast<int32_t>((uint64_t{w} * above) >> 32) * d;
		const int32_t pixels = (front - back) * d;
		if (pixels == 0) {
			continue; // possible near the start
		}
		if ((front - perc.latest) * d > 0 || (back - perc.earliest) * d < 0) {
			break;
		}
		while ((front - front_searched) * d > 0) {
			total += byte_at(front_searched);
			front_searched += d;
		}
		while ((back - back_searched) * d > 0) {
			total -= byte_at(back_searched);
			back_searched += d;
		}
		while ((back - back_searched) * d < 0) {
			back_searched -= d; // the decrement before the read, in this one case
			total += byte_at(back_searched);
		}
		if (total * best_pixels >= best_total * pixels) {
			best_total = total;
			best = static_cast<int32_t>(w);
			best_pixels = pixels;
		}
	}
	return best;
}

/// @brief The new head's start, the beam's back edge, held to the waveform's first frame in @p direction (B4).
[[nodiscard]] constexpr int32_t back_edge(int32_t best, Ratio ratio, int64_t position_frames, Direction direction,
                                          int32_t length_frames) {
	const auto d = static_cast<int32_t>(direction);
	const int64_t below = static_cast<int64_t>(ratio.raw()) - (1 << 24);
	auto back = static_cast<int32_t>(position_frames
	                                 + static_cast<int64_t>(static_cast<int32_t>((int64_t{best} * below) >> 25) * d));
	const int32_t first = direction == Direction::FORWARD ? 0 : length_frames - 1;
	if ((back - first) * d < 0) {
		back = first;
	}
	return back;
}

/// @brief The hop and its crossfade for best width @p best (B5): the hop at least 2 frames (decision 3), the
/// crossfade its share of the hop plus the fixed part, at most half the hop, taken from the hop, and both held to
/// @p ceiling.
[[nodiscard]] inline HopLengths hop_lengths(int32_t best, Increment increment, const HopShape& shape, int32_t ceiling) {
	auto hop = static_cast<int32_t>((uint64_t{static_cast<uint32_t>(best)} << 24) / increment.raw());
	hop = std::max(hop, 2);
	auto crossfade =
	    static_cast<uint32_t>(mul_r(Q<31>::from_raw(hop), shape.crossfade_share).raw() + shape.crossfade_quarters * 4);
	crossfade = std::min(crossfade, static_cast<uint32_t>(hop >> 1));
	hop -= static_cast<int32_t>(crossfade);
	hop = std::min(hop, ceiling);
	const int32_t xf = std::min(hop, static_cast<int32_t>(crossfade));
	return {.hop_frames = hop, .crossfade_frames = xf, .ramp = CrossfadeRamp::over_hop(xf)};
}

/// @brief Whether a head starting at @p back would start at or past the waveform's end (B6).
[[nodiscard]] constexpr bool past_end(int32_t back, Direction direction, int32_t length_frames) {
	const auto d = static_cast<int32_t>(direction);
	const int32_t end = direction == Direction::FORWARD ? length_frames : -1;
	return (back - end) * d >= 0;
}

/// @brief Near a loop end with a pre-margin (B7): a crossfade into the pre-margin once the loop end is under
/// kLoopCrossfadeReach output frames away and there is waveform where it would start; else, further off, a ceiling on
/// the hop so the next comes back in time.
[[nodiscard]] inline LoopPlan loop_crossfade(const LoopAhead& loop, int64_t position_frames, uint64_t combined,
                                             Ratio ratio, Increment increment, int32_t min_jittered,
                                             Direction direction, int32_t length_frames) {
	const auto d = static_cast<int32_t>(direction);
	const LoopPlan none{.kind = LoopPlan::Kind::NONE,
	                    .start_frame = 0,
	                    .crossfade_frames = 0,
	                    .hop_frames = 0,
	                    .ceiling = INT32_MAX,
	                    .align = false};
	const auto source =
	    static_cast<int32_t>(static_cast<int64_t>(static_cast<uint32_t>(loop.loop_end_frame)) - position_frames) * d;
	if (source < 0) {
		return none; // past the loop end: a clip that has just stopped
	}
	const auto out =
	    static_cast<int32_t>(((uint64_t{static_cast<uint32_t>(source)} << 24) + (combined >> 1)) / combined);
	if (out >= kLoopCrossfadeReach) {
		LoopPlan p = none;
		p.kind = LoopPlan::Kind::CEILING;
		p.ceiling = out - kLoopCrossfadeReach + 32;
		return p;
	}
	int32_t into = out;
	if (increment.raw() != (1u << 24)) {
		into =
		    static_cast<int32_t>(((uint64_t{static_cast<uint32_t>(source)} << 24) + (ratio.raw() >> 1)) / ratio.raw());
	}
	const int32_t start = loop.loop_start_frame - into * d;
	const int32_t first = direction == Direction::FORWARD ? 0 : length_frames - 1;
	if ((start - first) * d < 0) {
		return none;
	}
	const int32_t crossfade = std::max(out, 10);
	return {.kind = LoopPlan::Kind::CROSSFADE,
	        .start_frame = start,
	        .crossfade_frames = crossfade,
	        .hop_frames = std::max(min_jittered >> 2, crossfade),
	        .ceiling = INT32_MAX,
	        .align = !loop.cache_in_use};
}

/// @brief A start before the start marker whose frame is not resident moves to the marker (B8).
[[nodiscard]] constexpr int32_t clamp_start(int32_t start_frame, int32_t start_marker_frame, Direction direction,
                                            bool start_resident) {
	const auto d = static_cast<int32_t>(direction);
	return ((start_frame - start_marker_frame) * d < 0 && !start_resident) ? start_marker_frame : start_frame;
}

/// @brief The next hop (B2 to B7): a loop crossfade where B7 plans one, else the head at the back edge of the best
/// width, or past the end.
[[nodiscard]] inline HopPlan plan_hop(const HopRequest& request, const PercWindow* perc) {
	int32_t ceiling = INT32_MAX;
	if (request.loop) {
		const LoopPlan lp =
		    loop_crossfade(*request.loop, request.position_frames, request.combined, request.ratio, request.increment,
		                   request.shape.min_width, request.direction, request.length_frames);
		if (lp.kind == LoopPlan::Kind::CROSSFADE) {
			return {.kind = HopPlan::Kind::LOOP_CROSSFADE,
			        .start_frame = lp.start_frame,
			        .hop_frames = lp.hop_frames,
			        .crossfade_frames = lp.crossfade_frames,
			        .ramp = CrossfadeRamp::over_loop(lp.crossfade_frames),
			        .align = lp.align,
			        .max_hop_frames = INT32_MAX};
		}
		ceiling = lp.ceiling;
	}
	const Widths widths = repitched(request.shape, request.increment);
	const int32_t best = perc != nullptr
	                         ? best_width(*perc, widths, request.ratio, request.position_frames, request.direction)
	                         : widths.best;
	const int32_t back =
	    back_edge(best, request.ratio, request.position_frames, request.direction, request.length_frames);
	const HopLengths lengths = hop_lengths(best, request.increment, request.shape, ceiling);
	return {.kind = past_end(back, request.direction, request.length_frames) ? HopPlan::Kind::PAST_END
	                                                                         : HopPlan::Kind::HEAD,
	        .start_frame = back,
	        .hop_frames = lengths.hop_frames,
	        .crossfade_frames = lengths.crossfade_frames,
	        .ramp = lengths.ramp,
	        .align = true,
	        .max_hop_frames = ceiling};
}

} // namespace deluge::dsp::fixed::timestretch
