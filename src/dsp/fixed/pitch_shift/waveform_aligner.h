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

#include "dsp/fixed/pitch_shift/input_history.h"
#include "dsp/fixed/timestretch/waveform_search.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>

/// @file
/// @brief Lining a new hop's head up with the old one: the offset at which their waveforms match best.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief Where the aligner settled: whole frames to move the new head by, and the sub-sample phase to add, Q24.
struct Alignment {
	int32_t offset = 0;
	int32_t phase = 0;
};

/// @brief Lines the new head up with the old one by matching their waveforms.
/// @details Each head's waveform is summarised as kMovingAverages back-to-back totals of its frames' high words. The
/// aligner slides the new head's window a frame at a time, keeping running totals, and tracks the offset whose totals
/// differ least from the old head's. Where the totals' summed change from the old head's flips sign, the match is
/// placed between the two frames by the changes' sizes. After two flips, counted across both directions, it stops.
///
/// It searches one way and then the other. Searching forward first, a first step that matches worse than where it
/// started turns it round at once; searching backward first (pitching down) it searches backward only, with no early
/// turn (quirk Q10, docs/dev/live_pitch_shifter_quirks.md). Where too little of the history
/// has been written to hold the new head's window it does not search.
class WaveformAligner {
public:
	/// @brief The search's placement and extent.
	struct Search {
		uint32_t start = 0;              ///< The new head's window's first frame, a ring index.
		int32_t start_offset = 0;        ///< The window's start ahead of the head itself, in frames.
		int32_t length = 0;              ///< Each moving average's frames, at least 1.
		int32_t size = 0;                ///< The frames to search each way.
		int32_t direction = 1;           ///< 1 forward first, −1 backward first.
		int32_t directions_searched = 0; ///< 0 to search both ways, 1 to search the first only.
		bool sub_sample = false;         ///< Whether to place the match between frames (a shifted rate).
	};

	/// @brief The offset and phase that line @p search's window up with @p old_totals.
	/// @param latest_frames The frames written to the history, as the shifter counts them.
	template <typename Frame>
	[[nodiscard]] static Alignment align(const InputHistory<Frame>& history, const Search& search,
	                                     const MovingAverages& old_totals, uint64_t latest_frames) {
		const int32_t window = search.length * static_cast<int32_t>(kMovingAverages);
		// Too little written to hold the window, behind it or across it: no search
		if ((static_cast<uint32_t>(search.start - search.start_offset) & (kHistoryFrames - 1)) >= latest_frames) {
			return {};
		}
		if ((static_cast<uint32_t>(latest_frames - search.start) & (kHistoryFrames - 1))
		    < static_cast<uint32_t>(window)) {
			return {};
		}
		const RingReader<Frame> reader{&history, search, latest_frames};
		const timestretch::SearchShape shape{.flips_to_stop = 2,
		                                     .first_direction = search.direction,
		                                     .turns = search.directions_searched == 0,
		                                     .sub_sample = search.sub_sample};
		const timestretch::SearchResult found = timestretch::search(
		    reader, shape, old_totals, history.moving_averages(RingPosition{search.start}, search.length),
		    search.length, search.size);
		return {.offset = found.offset, .phase = found.phase};
	}

private:
	/// @brief The history read in ring order from the search's window, as the shared search reads frames.
	template <typename Frame>
	struct RingReader {
		const InputHistory<Frame>* history;
		Search search;
		uint64_t latest{};

		[[nodiscard]] std::optional<int32_t> folded(int32_t frame) const {
			return history->folded_high_word(RingPosition{search.start} + frame);
		}
		/// How far the window may slide: forward to the latest frame, or back to the history's start.
		[[nodiscard]] int32_t reach(int32_t direction, int32_t first_read) const {
			if (direction == 1) {
				return static_cast<int32_t>(
				    static_cast<uint32_t>(latest - (RingPosition{search.start} + first_read).index())
				    & (kHistoryFrames - 1));
			}
			if (latest < kHistoryFrames) {
				// Back to the history's start, less the frame the window already stepped back (quirk Q4,
				// docs/dev/live_pitch_shifter_quirks.md); none left reads as 0
				return std::max(static_cast<int32_t>(search.start) - search.start_offset - 1, 0);
			}
			return search.size;
		}
	};
	static_assert(std::is_same_v<MovingAverages, timestretch::Averages>);
};

} // namespace deluge::dsp::fixed::pitch_shift
