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

#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "dsp-next/fixed/timestretch/crossfade.h" // totalDifferenceAbs, totalChange

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

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
/// turn (quirk Q10, docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md). Where too little of the history
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
		Run run(history, search, old_totals, latest_frames);
		return run.result();
	}

private:
	/// @brief One alignment's search, pass by pass.
	template <typename Frame>
	class Run {
	public:
		Run(const InputHistory<Frame>& history, const Search& search, const MovingAverages& old_totals,
		    uint64_t latest_frames)
		    : history_(&history), search_(search), old_(old_totals), latest_(latest_frames),
		      new_totals_(history.moving_averages(RingPosition{search.start}, search.length)),
		      best_difference_(totalDifferenceAbs(old_, new_totals_)), initial_change_(totalChange(old_, new_totals_)),
		      direction_(search.direction), directions_searched_(search.directions_searched) {}

		[[nodiscard]] Alignment result() {
			while (true) {
				const Outcome outcome = pass();
				if (outcome == Outcome::STOP) {
					break;
				}
				if (outcome == Outcome::TURN) {
					direction_ = -direction_; // Turned at the first step: this direction is not yet searched
					continue;
				}
				++directions_searched_;
				if (directions_searched_ >= 2) {
					break;
				}
				direction_ = -direction_;
			}
			return found_;
		}

	private:
		enum class Outcome : uint8_t { STOP, NEXT_DIRECTION, TURN };

		using Reads = std::array<RingPosition, kMovingAverages + 1>;

		/// @brief One direction's slide from the window's start.
		Outcome pass() {
			int32_t last_change = initial_change_;
			Reads read{};
			read[0] = RingPosition{search_.start};
			if (direction_ == -1) {
				read[0] = read[0] - 1;
			}
			MovingAverages running = new_totals_;
			for (std::size_t i = 0; i < kMovingAverages; ++i) {
				read[i + 1] = read[i] + search_.length;
			}
			const int32_t here = std::min(search_.size, reach(read));
			if (here <= 0) {
				return Outcome::NEXT_DIRECTION;
			}
			const int32_t end = here * direction_;

			int32_t offset = 0;
			do {
				slide(read, running);
				const int32_t difference = totalDifferenceAbs(old_, running);
				if (offset == 0 && direction_ == 1 && directions_searched_ == 0 && difference > best_difference_) {
					return Outcome::TURN;
				}
				const int32_t next = offset + direction_;
				const bool best_here = difference < best_difference_;
				if (best_here) {
					best_difference_ = difference;
					found_.offset = next;
				}
				const int32_t change = totalChange(old_, running);
				if ((change >= 0) != (last_change >= 0)) {
					if (search_.sub_sample && (best_here || offset == found_.offset)) {
						place_between(change, last_change, best_here);
					}
					if (++sign_flips_ >= 2) {
						return Outcome::STOP;
					}
				}
				offset = next;
				last_change = change;
			} while (offset != end);
			return Outcome::NEXT_DIRECTION;
		}

		/// @brief How far this direction may slide: back to the history's start, or forward to the latest frame.
		[[nodiscard]] int32_t reach(const Reads& read) const {
			if (direction_ == 1) {
				return static_cast<int32_t>(static_cast<uint32_t>(latest_ - read[kMovingAverages].index())
				                            & (kHistoryFrames - 1));
			}
			if (latest_ < kHistoryFrames) {
				// Back to the history's start, less the frame the window already stepped back (quirk Q4,
				// docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md); none left reads as 0
				return std::max(static_cast<int32_t>(search_.start) - search_.start_offset - 1, 0);
			}
			return search_.size;
		}

		/// @brief Slides the window one frame: each frame read moves from one average's total to the next's.
		void slide(Reads& read, MovingAverages& running) const {
			for (std::size_t i = 0; i < kMovingAverages + 1; ++i) {
				const int32_t value = history_->folded_high_word(read[i]);
				read[i] = read[i] + direction_;
				if (i < kMovingAverages) {
					running[i] -= value * direction_;
				}
				if (i > 0) {
					running[i - 1] += value * direction_;
				}
			}
		}

		/// @brief The match placed between this frame and the last, by the sizes of the changes either side of the
		/// sign's flip.
		void place_between(int32_t change, int32_t last_change, bool best_here) {
			const auto here = static_cast<uint32_t>(std::abs(change));
			const auto last = static_cast<uint32_t>(std::abs(last_change));
			found_.phase =
			    static_cast<int32_t>((static_cast<uint64_t>(last) << 24) / static_cast<uint32_t>(last + here));
			if (direction_ == -1) {
				found_.phase = kOneFrame - found_.phase;
			}
			if (best_here != (direction_ == -1)) {
				--found_.offset;
			}
		}

		static constexpr int32_t kOneFrame = 1 << 24;

		const InputHistory<Frame>* history_;
		Search search_;
		MovingAverages old_;
		uint64_t latest_;
		MovingAverages new_totals_;
		int32_t best_difference_;
		int32_t initial_change_;
		int32_t direction_;
		int32_t directions_searched_;
		int32_t sign_flips_ = 0;
		Alignment found_{};
	};
};

} // namespace deluge::dsp::fixed::pitch_shift
