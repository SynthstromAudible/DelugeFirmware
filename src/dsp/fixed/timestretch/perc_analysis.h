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
#include "dsp/fixed/pitch_shift/slope_follower.h"
#include "dsp/fixed/sample_read/frame_source.h"

#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// @brief The time stretcher's transient ("perc") analysis: how percussive the sample is around each 128-frame pixel,
/// which the hop planner reads to place hops on transients (P1 to P3, P5 of docs/dev/time_stretcher_contracts.md). Its
/// slope follower and percussiveness are the pitch shifter's, shared (decision 6).

using pitch_shift::SlopeFollower;
using sample_read::Direction;
using sample_read::FrameRun;
using sample_read::Increment;

/// Frames per perc pixel.
inline constexpr int32_t kPercPixelFrames = 128;
inline constexpr int32_t kPercPixelShift = 7;

/// @brief A perc zone's analysis state: the follower's, and the slope of the frame before (P5: one per zone).
struct PercState {
	SlopeFollower::State follower;
	Q<31> previous_slope{};
};

/// @brief How a sample's perc store is laid out for one direction (P1).
struct PercStoreLayout {
	int32_t bytes;  ///< One per pixel, at least 1.
	bool chunked;   ///< From half a chunk of bytes, the store is kept in chunks.
	int32_t chunks; ///< 0 when not chunked.
};

/// @brief The perc store for a sample of @p length_frames, in chunks of @p chunk_size bytes (a power of two) (P1).
[[nodiscard]] constexpr PercStoreLayout perc_store_layout(int32_t length_frames, int32_t chunk_size) {
	const int32_t bytes = std::max(((length_frames - 1) >> kPercPixelShift) + 1, 1);
	const bool chunked = bytes >= (chunk_size >> 1);
	return {.bytes = bytes, .chunked = chunked, .chunks = chunked ? ((bytes - 1) / chunk_size) + 1 : 0};
}

/// The unmasked words are the sample's own, so the fold crosses into Q31 once.
/// @brief Frame @p k of @p run folded to one channel for the follower (P2): a quarter of each channel's unmasked
/// word, summed.
[[nodiscard]] inline Q<31> fold(const FrameRun& run, int32_t k) {
	int32_t x = sample_read::frame_word(run, k, 0) >> 2;
	if (run.channels == 2) {
		x += sample_read::frame_word(run, k, 1) >> 2;
	}
	return Q<31>::from_raw(x);
}

/// @brief Analyses @p frames frames of @p run, which starts at frame @p first_frame, moving @p state on (P2, P3). Where
/// the position after a frame is a pixel's middle (frame 64 of it forwards, 63 reversed), that pixel's byte in
/// @p pixels (whose element 0 is pixel @p pixel_origin) is the percussiveness of the slope against the frame before's.
inline void analyse(const FrameRun& run, int32_t first_frame, int32_t frames, Direction direction, PercState& state,
                    std::span<uint8_t> pixels, int32_t pixel_origin) {
	const auto step = static_cast<int32_t>(direction);
	const int32_t middle = (kPercPixelFrames >> 1) - (direction == Direction::REVERSE ? 1 : 0);
	for (int32_t k = 0; k < frames; k++) {
		const Q<31> previous = state.previous_slope;
		const Q<31> slope = SlopeFollower::step({}, {}, state.follower, fold(run, k));
		const int32_t after = first_frame + (k + 1) * step;
		if ((after & (kPercPixelFrames - 1)) == middle) {
			const int32_t index = (after >> kPercPixelShift) - pixel_origin;
			if (index >= 0 && index < static_cast<int32_t>(pixels.size())) {
				pixels[index] = pitch_shift::percussiveness(slope, previous);
			}
		}
		state.previous_slope = slope;
	}
}

/// @brief A perc zone: the frames it covers, from its start to its end in its direction, and how many at its start a
/// zone growing into it may overwrite.
struct PercZoneSpan {
	int32_t start;
	int32_t end;
	int32_t replaceable;
};

/// @brief Whether a fill continues a zone or opens one, which, and where it fills from (P5).
struct ZoneStep {
	enum class Kind : uint8_t { CONTINUE, NEW } kind;
	int32_t zone;
	int32_t fill_from;
};

/// How far short of a fill's start a zone may end and still be continued (P5).
inline constexpr int32_t kZoneReach = 2048;

/// @brief The zone a fill from @p from_frame continues, or where it opens one, among @p zones (sorted by start): the
/// zone it starts in, or one ending at most kZoneReach frames short of it, continued from its end (P5).
[[nodiscard]] inline ZoneStep zone_for_fill(std::span<const PercZoneSpan> zones, int32_t from_frame,
                                            Direction direction) {
	const bool forward = direction == Direction::FORWARD;
	const int32_t key = forward ? from_frame + 1 : from_frame;
	int32_t lower = 0; // the first zone whose start is at or past the key
	while (lower < static_cast<int32_t>(zones.size()) && zones[lower].start < key) {
		lower++;
	}
	const int32_t i = forward ? lower - 1 : lower;
	if (i >= 0 && i < static_cast<int32_t>(zones.size())
	    && (zones[i].end - from_frame) * static_cast<int32_t>(direction) >= -kZoneReach) {
		return {.kind = ZoneStep::Kind::CONTINUE, .zone = i, .fill_from = zones[i].end};
	}
	return {.kind = ZoneStep::Kind::NEW, .zone = forward ? i + 1 : i, .fill_from = from_frame};
}

/// @brief Whether @p growing, filled to its end, reaches past @p next's replaceable start, so it overwrites it and the
/// two merge (P5).
[[nodiscard]] constexpr bool overwrites(const PercZoneSpan& growing, const PercZoneSpan& next, Direction direction) {
	const auto d = static_cast<int32_t>(direction);
	return (growing.end - (next.start + next.replaceable * d)) * d >= 0;
}

} // namespace deluge::dsp::fixed::timestretch
