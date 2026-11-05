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

#include "definitions_cxx.hpp" // kMaxSampleValue, TimeStretch::Crossfade
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "dsp-next/fixed/pitch_shift/percussive_search.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"
#include "dsp-next/fixed/pitch_shift/play_head.h"
#include "dsp-next/fixed/pitch_shift/waveform_aligner.h"

#include <algorithm>
#include <cstdint>

/// @file
/// @brief The live pitch shifter's next hop: where its new head starts, and how long the hop runs.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief A hop as the planner places it.
struct HopPlan {
	RingPosition cursor;              ///< The new head's first frame.
	uint32_t phase = 0;               ///< The new head's sub-sample phase, Q24 (repitching).
	HeadKind kind = HeadKind::DIRECT; ///< Direct when the shift is none.
	int32_t hop_length = 0;           ///< Output samples until the next hop.
	PitchShiftTuning::Coefficients
	    tuning{}; ///< The tuning at the hop's shift: the next crossfade, the onset threshold.
};

/// @brief Plans each hop of the live pitch shifter.
/// @details The new head goes back from now, pitching up, by @ref PercussiveSearch's distance (at least far enough for
/// the alignment's window); pitching down (or at no shift), it starts just behind the latest input. Its window is then
/// lined up with the old head's by @ref WaveformAligner, when the old head is repitching and there is room, and the
/// alignment's sub-sample phase is added to the old head's. Pitching up, the hop lasts the time the new head takes to
/// close on now, less the next crossfade, held between 100 samples (quirk Q5,
/// docs/dev/live_pitch_shifter_quirks.md) and the tuning's longest; pitching down, it is the
/// tuning's longest.
class HopPlanner {
public:
	/// @brief The next hop.
	/// @param older The old head, as it is after the hop's swap.
	/// @param this_crossfade The crossfade the hop starts with, in output samples.
	/// @param samples_into_hop The output samples the hop ending has run.
	/// @param now_frames, latest_frames The frames written at the hop's moment and at the latest input, as the shifter
	///        counts them.
	template <typename Frame>
	[[nodiscard]] static HopPlan plan(int32_t phase_increment, const InputHistory<Frame>& history,
	                                  const PlayHeadState& older, uint32_t this_crossfade, int32_t samples_into_hop,
	                                  uint64_t now_frames, uint64_t latest_frames, Cong& noise) {
		HopPlan plan{};
		plan.tuning = PitchShiftTuning::coefficients({.phase_increment = phase_increment});
		const auto averages = static_cast<int32_t>(kMovingAverages);

		// The moving averages' length, at the shift's rate, then fitted to the input written ahead of the old head
		auto length = static_cast<int32_t>((static_cast<uint64_t>(phase_increment) * kMovingAverageLength) >> 24);
		length = std::clamp<int32_t>(length, 1, kMovingAverageLength * 2);
		const auto crossfade_source =
		    static_cast<int32_t>((static_cast<uint64_t>(this_crossfade) * phase_increment) >> 24);
		const auto room =
		    static_cast<int32_t>(static_cast<uint32_t>(latest_frames - older.cursor.index()) & (kHistoryFrames - 1));
		const int32_t end_offset = std::min((crossfade_source >> 1) + ((length * averages) >> 1), room);
		length = std::min(length, end_offset >> 1);
		// The averages' whole window, an int32 product as the frame arithmetic below widens it
		const int32_t window = length * averages;
		const int32_t start_offset = end_offset - window;

		MovingAverages old_totals{};
		if (length != 0) {
			old_totals = history.moving_averages(older.cursor + start_offset, length);
		}

		WaveformAligner::Search search{
		    .start_offset = start_offset, .length = length, .sub_sample = phase_increment != kMaxSampleValue};
		uint32_t cursor = 0;
		if (phase_increment > kMaxSampleValue) {
			int32_t how_far_back = PercussiveSearch::frames_back(plan.tuning, now_frames, history, noise);
			plan.hop_length = static_cast<int32_t>((static_cast<uint64_t>(how_far_back) << 24)
			                                           / static_cast<uint32_t>(phase_increment - kMaxSampleValue)
			                                       - plan.tuning.crossfade_length);
			if (plan.hop_length < kMinHop) {
				plan.hop_length = kMinHop;
			}
			else if (plan.hop_length > plan.tuning.max_hop_length) {
				plan.hop_length = plan.tuning.max_hop_length;
			}
			// Far enough back for the alignment's window
			const auto min_back = static_cast<int32_t>(now_frames - latest_frames + start_offset + window);
			how_far_back = std::max(how_far_back, min_back);
			// A negative distance wraps here, as it always has
			if (static_cast<uint64_t>(how_far_back) > now_frames) {
				how_far_back = static_cast<int32_t>(now_frames);
			}
			if (length != 0) {
				search.start = static_cast<uint32_t>((now_frames - how_far_back + start_offset) & (kHistoryFrames - 1));
				search.size = std::min(kUpSearch, plan.hop_length);
				search.direction = 1;
				search.directions_searched = 0;
			}
			cursor = static_cast<uint32_t>(now_frames - how_far_back) & (kHistoryFrames - 1);
		}
		else {
			plan.hop_length = plan.tuning.max_hop_length;
			if (length != 0) {
				search.start = static_cast<uint32_t>((latest_frames - window) & (kHistoryFrames - 1));
				search.size = std::min(kDownSearch, samples_into_hop);
				search.direction = -1;
				search.directions_searched = 1;
			}
			cursor = static_cast<uint32_t>(latest_frames - window - start_offset) & (kHistoryFrames - 1);
		}

		Alignment alignment{};
		if (length != 0 && older.kind != HeadKind::DIRECT) {
			alignment = WaveformAligner::align(history, search, old_totals, latest_frames);
		}
		// The alignment's phase on top of the old head's, a whole frame carried into the offset
		auto phase = static_cast<int32_t>(static_cast<uint32_t>(alignment.phase) + older.phase);
		int32_t offset = alignment.offset;
		if (phase >= kMaxSampleValue) {
			phase -= kMaxSampleValue;
			++offset;
		}

		if (phase_increment == kMaxSampleValue) {
			plan.kind = HeadKind::DIRECT;
			plan.cursor = RingPosition{static_cast<uint32_t>(now_frames)};
		}
		else {
			plan.kind = HeadKind::REPITCHING;
			plan.cursor = RingPosition{cursor + static_cast<uint32_t>(offset)};
			plan.phase = static_cast<uint32_t>(phase);
			// Far enough behind the latest input that the rest of the block cannot read past it: the head moves the
			// phase plus the remaining samples' steps, in whole frames
			const uint64_t remaining = latest_frames - now_frames;
			const auto advance =
			    static_cast<uint32_t>((plan.phase + (remaining * static_cast<uint32_t>(phase_increment))) >> 24);
			const RingPosition latest{static_cast<uint32_t>(latest_frames)};
			const uint32_t behind = latest.since(plan.cursor);
			if (behind > kHistoryFrames / 2 || behind < advance) {
				plan.cursor = latest - static_cast<int32_t>(advance);
			}
		}
		return plan;
	}

private:
	/// The moving averages' length at no shift, in input frames.
	static constexpr int32_t kMovingAverageLength = TimeStretch::Crossfade::kMovingAverageLength;
	/// The shortest pitched-up hop, in output samples (quirk Q5).
	static constexpr int32_t kMinHop = 100;
	/// The alignment's reach each way, in input frames: down to about 45 Hz.
	static constexpr int32_t kUpSearch = 490;
	static constexpr int32_t kDownSearch = 980;
};

} // namespace deluge::dsp::fixed::pitch_shift
