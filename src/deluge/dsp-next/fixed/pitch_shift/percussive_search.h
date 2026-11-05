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
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"

#include <cstdint>

/// @file
/// @brief Where a pitched-up hop starts: the stretch back from now with the most percussive input.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief The pitched-up hop's placement: how far back from now the new head starts.
/// @details It searches back from now over the percussiveness entries, between the tuning's nearest edge (moved out by
/// a random part of itself, so the hops do not settle into one length and sound as a tone) and its furthest, for the
/// stretch ending at now whose average percussiveness is highest, comparing averages by cross-multiplying their totals
/// and lengths. The search stops early where it would reach before the history's start.
class PercussiveSearch {
public:
	/// @brief The frames back from @p now_frames the new head starts at: a whole number of percussiveness entries.
	/// @param now_frames The frames written up to now, as the shifter counts them.
	/// @param noise The voice's noise: one draw, for the random part of the nearest edge.
	template <typename Frame>
	[[nodiscard]] static int32_t frames_back(const PitchShiftTuning::Coefficients& tuning, uint64_t now_frames,
	                                         const InputHistory<Frame>& history, Cong& noise) {
		// A random fraction of the search length: the Q31 noise times the random element moved up 8, Q30, times the
		// length's word read as Q31 (an integer count, so the product is its fraction at Q29), ×4
		const auto fraction =
		    mul(Q<31>::from_raw(static_cast<int32_t>(noise.next())), Q<31>::from_raw(tuning.random_element << 8));
		const int32_t min_search =
		    tuning.min_search + scale_shl_wrap<2>(mul(Q<31>::from_raw(tuning.min_search), fraction)).raw();

		// The search in percussiveness entries, back from the entry holding now
		const auto entry_now = static_cast<int32_t>((now_frames + kEntryFrames - 1) >> kPercussivenessStepMagnitude);
		int32_t back_edge = min_search >> kPercussivenessStepMagnitude;
		const int32_t far_edge = tuning.max_search >> kPercussivenessStepMagnitude;
		int32_t searched = 0;
		uint32_t total = 0;
		// The best average so far, as its total over its length: none yet is 0 over 1
		uint32_t best_total = 0;
		uint32_t best_length = 1;
		int32_t best = back_edge;

		for (; back_edge < far_edge; ++back_edge) {
			while (searched < back_edge) {
				++searched;
				if (searched > entry_now) {
					return best << kPercussivenessStepMagnitude; // The history starts here
				}
				total += history.percussiveness_at(static_cast<uint32_t>(entry_now - searched));
			}
			// total / searched > best_total / best_length, exactly; an empty stretch (0 over 0) never wins
			if (uint64_t{total} * best_length > uint64_t{best_total} * static_cast<uint32_t>(searched)) {
				best_total = total;
				best_length = static_cast<uint32_t>(searched);
				best = searched;
			}
		}
		return best << kPercussivenessStepMagnitude;
	}

private:
	static constexpr int32_t kEntryFrames = 1 << kPercussivenessStepMagnitude;
};

} // namespace deluge::dsp::fixed::pitch_shift
