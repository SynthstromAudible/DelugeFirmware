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

/// @file
/// @brief The pitch shifter's hop end before its planner, verbatim from the shifter (473eeb13b) but for its inputs and
/// outputs: the shifter's members became locals and results, the old head a parameter, and the new head's placement the
/// plan it applied. The planner and its search and aligner are proven against it.

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/noise/cong.h"
#include "dsp-next/fixed/pitch_shift/input_history.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"
#include "dsp-next/fixed/pitch_shift/play_head.h"
#include "dsp-next/fixed/timestretch/crossfade.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// The legacy arithmetic, verbatim.
// NOLINTBEGIN

namespace deluge::synth::reference_hop {

/// @brief What the old hop end set: the new head's placement, and the hop's length, threshold and next crossfade.
struct ReferencePlan {
	uint32_t cursor = 0;
	uint32_t phase = 0;
	dsp::fixed::pitch_shift::HeadKind kind = dsp::fixed::pitch_shift::HeadKind::DIRECT;
	int32_t hop_length = 0;
	int32_t percussive_threshold = 0;
	int32_t next_crossfade_length = 0;
};

/// @brief The history as the old hop end read it: by ring index.
template <typename Frame>
struct HistoryByIndex {
	const dsp::fixed::pitch_shift::InputHistory<Frame>& h;
	explicit operator bool() const { return true; }
	[[nodiscard]] dsp::fixed::pitch_shift::MovingAverages moving_averages(uint32_t start, int32_t length) const {
		return h.moving_averages(dsp::fixed::pitch_shift::RingPosition{start}, length);
	}
	[[nodiscard]] uint8_t percussiveness_at(uint32_t entry) const { return h.percussiveness_at(entry); }
	[[nodiscard]] int32_t folded_high_word(uint32_t pos) const {
		return h.folded_high_word(dsp::fixed::pitch_shift::RingPosition{pos});
	}
};

/// @brief The old hop end, from the swap of heads (@p older is the old head after it) to the new head's placement.
template <typename Frame>
ReferencePlan reference_plan(int32_t phase_increment, const dsp::fixed::pitch_shift::InputHistory<Frame>& history,
                             const dsp::fixed::pitch_shift::PlayHeadState& older, uint32_t this_crossfade_length,
                             int32_t samples_into_hop, uint64_t num_raw_samples_processed_at_now_time,
                             uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise) {
	const HistoryByIndex<Frame> live_input{history};

	// int32_t numChannelsNow = numChannels;

	// D_PRINTLN("");
	// D_PRINTLN(phaseIncrement);

	// D_PRINTLN((uint32_t)(liveInputBuffer->numRawSamplesProcessed - playHeads[PLAY_HEAD_NEWER].rawBufferReadPos) &
	// (kInputRawBufferSize - 1)); // How far behind raw buffer is being read

	// What was new is now old
	uint32_t newer_read_pos = older.cursor.index();
	int32_t perc_threshold_for_cut;
	int32_t next_crossfade_length;
	int32_t samples_til_hop_end;

	const dsp::fixed::pitch_shift::PitchShiftTuning::Coefficients tuning =
	    dsp::fixed::pitch_shift::PitchShiftTuning::coefficients({.phase_increment = phase_increment});
	int32_t min_search = tuning.min_search;
	int32_t max_search = tuning.max_search;
	perc_threshold_for_cut = tuning.percussive_threshold;
	next_crossfade_length = tuning.crossfade_length;
	int32_t max_hop_length = tuning.max_hop_length;
	int32_t random_element = tuning.random_element;

	/*
	minSearch = StorageManager::devVarA << 7;
	maxSearch = StorageManager::devVarB << 7;
	nextCrossfadeLength = StorageManager::devVarC << 4;
	percThresholdForCut = StorageManager::devVarD;
	maxHopLength = StorageManager::devVarE * 100;
	//int32_t searchSizeFactor = StorageManager::devVarF;
	randomElement = StorageManager::devVarG << 16;
	*/

	// Collect info on those moving average for the now-older play-head, which we're going to fade out -
	// so that we can then fine-tune the position of the new play-head to match it

	// First, work out the length we'd *like* to use for the moving averages
	int32_t length_per_moving_average =
	    ((uint64_t)phase_increment * TimeStretch::Crossfade::kMovingAverageLength) >> 24;
	length_per_moving_average = std::clamp<int32_t>(
	    length_per_moving_average, 1, TimeStretch::Crossfade::kMovingAverageLength * 2); // Keep things sensible

	// Ok, and this crossfade we're about to do, how long will it be in samples of (unpitched) source material?
	int32_t crossfade_length_samples_source = ((uint64_t)this_crossfade_length * phase_increment) >> 24;

	// What's the maximum amount further forward than that older play-head that data actually exists yet for us to
	// examine?
	int32_t max_offset_from_head =
	    (uint32_t)(num_raw_samples_processed_latest - older.cursor.index()) & (kInputRawBufferSize - 1);

	// Ok, work out the end-pos of our moving-averages region
	int32_t averages_end_offset_from_head =
	    (crossfade_length_samples_source >> 1)
	    + ((length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages) >> 1);
	averages_end_offset_from_head =
	    std::min(averages_end_offset_from_head,
	             max_offset_from_head); // And make sure it's not beyond the end of the existent data

	// We now know the length of the *total* moving-averages region, so divide down to get the length of *each*
	// moving-average region If commenting out this next line, must make sure we still don't search back before we
	// started writing to buffer
	length_per_moving_average =
	    std::min(length_per_moving_average,
	             averages_end_offset_from_head >> 1); // / TimeStretch::Crossfade::kNumMovingAverages

	int32_t averages_start_offset_from_head =
	    averages_end_offset_from_head - (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages);

	int32_t old_head_totals[TimeStretch::Crossfade::kNumMovingAverages];

	// Occasionally (if we've only just switched pitch shifting on), that'll be zero, so we can'd do the moving
	// averages. But if it's ok, then...
	if (length_per_moving_average) {
		int32_t averages_start_pos_old_head =
		    (older.cursor.index() + averages_start_offset_from_head) & (kInputRawBufferSize - 1);
		std::ranges::copy(live_input.moving_averages(averages_start_pos_old_head, length_per_moving_average),
		                  old_head_totals);
	}

	int32_t averages_start_pos_new_head;
	int32_t search_size;
	int32_t search_direction;
	int32_t num_full_directions_searched;

	int32_t how_far_back;

	// Now we're going to pick a position for the new play-head based on searching the percussiveness data. This is
	// totally different for pitching up vs down. And while we're at it, also decide our search parameters for our
	// fine-tuning of the new play head pos - if we're going to do that (we might not - if lengthPerMovingAverage == 0)

	// If pitch shifting up...
	if (phase_increment > kMaxSampleValue) {

		if (live_input) {

			// Search backwards, looking for the area (ending at now-time) with the highest average percussiveness.
			// This particularly helps to keep the hops from all happening at the same length, which leads to a clearly
			// audible sort of "tone". But, some sort of random element could actually work just as well - I haven't
			// tried yet.

			// A random fraction of the search length: the Q31 noise times the random element moved up 8, Q30, times the
			// length's word read as Q31 (an integer count, so the product is its fraction at Q29), ×4
			const auto fraction = mul(deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(noise.next())),
			                          deluge::dsp::Q<31>::from_raw(random_element << 8));
			min_search += scale_shl_wrap<2>(mul(deluge::dsp::Q<31>::from_raw(min_search), fraction)).raw();

			int32_t back_edge = min_search >> kPercBufferReductionMagnitude; // Pixellated
			int32_t how_far_back_searched = 0;
			int32_t perc_pos =
			    (num_raw_samples_processed_at_now_time + kPercBufferReductionSize - 1) >> kPercBufferReductionMagnitude;

			uint32_t total_perc = 0;
			float best_average = 0;
			int32_t best_how_far_back = min_search >> kPercBufferReductionMagnitude; // Pixellated

			while (back_edge < (max_search >> kPercBufferReductionMagnitude)) {

				while (how_far_back_searched < back_edge) {
					how_far_back_searched++;
					if (how_far_back_searched > perc_pos) {
						goto stopPercSearch;
					}
					int32_t perc_here = live_input.percussiveness_at((uint32_t)(perc_pos - how_far_back_searched));
					total_perc += perc_here;
				}

				float average_perc = (float)total_perc / how_far_back_searched;
				if (average_perc > best_average) {
					best_average = average_perc;
					best_how_far_back = how_far_back_searched;
				}

				// else if (averagePerc < bestAverage * 0.5) break; // Didn't help

				back_edge++;
			}

stopPercSearch:

			how_far_back = best_how_far_back << kPercBufferReductionMagnitude;
		}

		else {
			how_far_back = 1500;
		}

		samples_til_hop_end =
		    ((uint64_t)how_far_back << 24) / (uint32_t)(phase_increment - kMaxSampleValue) - next_crossfade_length;
		if (samples_til_hop_end < 100) {
			samples_til_hop_end = 100; // Must be 100, not 200. Otherwise, shifting up 2 octaves gets messed up. (Though
			                           // maybe not anymore?)
		}
		else if (samples_til_hop_end > max_hop_length) {
			samples_til_hop_end = max_hop_length;
		}

		int32_t min_distance_back = num_raw_samples_processed_at_now_time - num_raw_samples_processed_latest
		                            + averages_start_offset_from_head
		                            + (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages);
		how_far_back = std::max(how_far_back, min_distance_back);

		// NOLINTNEXTLINE(modernize-use-integer-sign-comparison): a negative howFarBack wraps, as it always has
		if (static_cast<uint64_t>(how_far_back) > num_raw_samples_processed_at_now_time) {
			how_far_back = num_raw_samples_processed_at_now_time;
		}

		if (length_per_moving_average) {
			averages_start_pos_new_head =
			    (num_raw_samples_processed_at_now_time - how_far_back + averages_start_offset_from_head)
			    & (kInputRawBufferSize - 1);
			search_size = 490; // Allow tracking down to about 45Hz
			search_size = std::min(search_size, samples_til_hop_end);
			num_full_directions_searched = 0;
			search_direction = 1;
		}

		newer_read_pos = (uint32_t)(num_raw_samples_processed_at_now_time - how_far_back) & (kInputRawBufferSize - 1);
	}

	// Or if pitch shifting down...
	else {
		samples_til_hop_end = max_hop_length;

		if (length_per_moving_average) {
			averages_start_pos_new_head = (num_raw_samples_processed_latest
			                               - length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
			                              & (kInputRawBufferSize - 1);
			search_size = 980; // Allow tracking down to about 45Hz
			search_size = std::min(search_size, samples_into_hop);
			num_full_directions_searched = 1;
			search_direction = -1;
		}

		newer_read_pos = (uint32_t)(num_raw_samples_processed_latest
		                            - (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
		                            - averages_start_offset_from_head)
		                 & (kInputRawBufferSize - 1);
	}

	int32_t best_offset = 0;

	int32_t additional_osc_pos = 0;

	// If we're all good to do the moving averages...
	if (length_per_moving_average && older.kind != dsp::fixed::pitch_shift::HeadKind::DIRECT) {

		// Ok, and now we're going to get that moving-average info for the new play-head in the position where we're
		// currently proposing we put it - but we're then going to experiment with shifting that, below

		if (((uint32_t)(averages_start_pos_new_head - averages_start_offset_from_head) & (kInputRawBufferSize - 1))
		    >= num_raw_samples_processed_latest) {
			goto stopSearch;
		}

		if (((uint32_t)(num_raw_samples_processed_latest - averages_start_pos_new_head) & (kInputRawBufferSize - 1))
		    < static_cast<uint32_t>(length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)) {
			goto stopSearch;
		}

		int32_t new_head_totals[TimeStretch::Crossfade::kNumMovingAverages];
		std::ranges::copy(live_input.moving_averages(averages_start_pos_new_head, length_per_moving_average),
		                  new_head_totals);

		int32_t best_difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_totals);
		int32_t times_sign_flipped = 0;

		int32_t initial_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_totals);

startSearch:
		int32_t last_total_change = initial_total_change;

		int32_t read_pos[TimeStretch::Crossfade::kNumMovingAverages + 1];

		read_pos[0] = averages_start_pos_new_head;
		if (search_direction == -1) {
			read_pos[0] = (uint32_t)(read_pos[0] - 1) & (kInputRawBufferSize - 1);
		}

		int32_t new_head_running_totals[TimeStretch::Crossfade::kNumMovingAverages];
		for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
			new_head_running_totals[i] = new_head_totals[i];
			read_pos[i + 1] = (uint32_t)(read_pos[i] + length_per_moving_average) & (kInputRawBufferSize - 1);
		}

		int32_t offset_now = 0;
		int32_t end_offset;

		int32_t search_size_boundary = search_size;
		if (search_direction == -1) {
			if (num_raw_samples_processed_latest < kInputRawBufferSize) { // Is this right?
				search_size_boundary =
				    averages_start_pos_new_head - averages_start_offset_from_head
				    - 1; // The -1 is becacuse of the 1 we subtract from readPos[0] above when searching left
				if (search_size_boundary <= 0) {
					goto searchNextDirection;
				}
			}
		}
		else {
			// readPos is sized [kNumMovingAverages + 1] and filled [0..kNumMovingAverages]
			// (above); indexing [kNumMovingAverages + 1] read one past the end — adjacent
			// stack garbage → wrong search boundary. The furthest filled read position is
			// readPos[kNumMovingAverages].
			search_size_boundary =
			    (uint32_t)(num_raw_samples_processed_latest - read_pos[TimeStretch::Crossfade::kNumMovingAverages])
			    & (kInputRawBufferSize - 1);
		}

		{
			int32_t search_size_here = std::min(search_size, search_size_boundary);
			// guard against potential infinite do/while loop below
			if (search_size_here <= 0) {
				goto searchNextDirection;
			}
			end_offset = search_size_here * search_direction;
		}

		do {

			for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages + 1; i++) {

				int32_t read_value = live_input.folded_high_word(read_pos[i]);

				read_pos[i] = (uint32_t)(read_pos[i] + search_direction) & (kInputRawBufferSize - 1);

				if (i < TimeStretch::Crossfade::kNumMovingAverages) {
					new_head_running_totals[i] -= read_value * search_direction;
				}

				if (i > 0) {
					new_head_running_totals[i - 1] += read_value * search_direction;
				}
			}

			int32_t difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_running_totals);

			// If our very first read is worse, let's switch search direction right now - that'll improve our odds
			if (offset_now == 0 && search_direction == 1 && !num_full_directions_searched
			    && difference_abs > best_difference_abs) {
				search_direction = -search_direction;
				goto startSearch;
			}

			int32_t new_offset_now = offset_now + search_direction;

			// Keep track of best match
			bool this_offset_is_best_match = (difference_abs < best_difference_abs);
			if (this_offset_is_best_match) {
				best_difference_abs = difference_abs;
				best_offset = new_offset_now;
			}

			int32_t this_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_running_totals);

			// If sign just flipped...
			if ((this_total_change >= 0) != (last_total_change >= 0)) {

				// Try going in between the samples for the most accurate positioning, lining-up-wise.
				// The benefit of this is visible on a spectrum analysis if you're pitching a high-pitched sine wave
				// right down, while also time stretching it
				if (phase_increment != kMaxSampleValue
				    && (this_offset_is_best_match || offset_now == best_offset)) { // If best was this one or last one
					uint32_t this_total_difference_abs = std::abs(this_total_change);
					uint32_t last_total_difference_abs = std::abs(last_total_change);
					additional_osc_pos = ((uint64_t)last_total_difference_abs << 24)
					                     / (uint32_t)(last_total_difference_abs + this_total_difference_abs);
					if (search_direction == -1) {
						additional_osc_pos = kMaxSampleValue - additional_osc_pos;
					}
					if (this_offset_is_best_match != (search_direction == -1)) {
						best_offset--;
					}
				}

				// After sign has flipped twice (in total, including both search directions), we can be fairly sure
				// we've found a good fit
				times_sign_flipped++;
				if (times_sign_flipped >= 2) {
					goto stopSearch;
				}
			}

			offset_now = new_offset_now;
			last_total_change = this_total_change;
		} while (offset_now != end_offset);

searchNextDirection:
		// Search the other direction if we haven't already
		num_full_directions_searched++;
		if (num_full_directions_searched < 2) {
			search_direction = -search_direction;
			goto startSearch;
		}
	}

stopSearch:

	additional_osc_pos += older.phase;
	if (additional_osc_pos >= kMaxSampleValue) {
		additional_osc_pos -= kMaxSampleValue;
		best_offset++;
	}

	newer_read_pos = (uint32_t)(newer_read_pos + best_offset) & (kInputRawBufferSize - 1);

	// If still here, use raw direct buffer - usually because we're pitching down

	ReferencePlan plan{.hop_length = samples_til_hop_end,
	                   .percussive_threshold = perc_threshold_for_cut,
	                   .next_crossfade_length = next_crossfade_length};
	if (phase_increment == kMaxSampleValue) {
		plan.cursor = static_cast<uint32_t>(num_raw_samples_processed_at_now_time) & (kInputRawBufferSize - 1);
		plan.kind = dsp::fixed::pitch_shift::HeadKind::DIRECT;
	}
	else {
		plan.cursor = newer_read_pos;
		plan.phase = static_cast<uint32_t>(additional_osc_pos);
		plan.kind = dsp::fixed::pitch_shift::HeadKind::REPITCHING;
	}
	return plan;
}

/// @brief The old hop end's percussive back-search, verbatim but for its inputs.
template <typename Frame>
int32_t reference_frames_back(int32_t min_search, int32_t max_search, int32_t random_element,
                              uint64_t num_raw_samples_processed_at_now_time,
                              const dsp::fixed::pitch_shift::InputHistory<Frame>& history, dsp::fixed::Cong& noise) {
	const HistoryByIndex<Frame> live_input{history};
	int32_t how_far_back;
	// A random fraction of the search length: the Q31 noise times the random element moved up 8, Q30, times the
	// length's word read as Q31 (an integer count, so the product is its fraction at Q29), ×4
	const auto fraction = mul(deluge::dsp::Q<31>::from_raw(static_cast<int32_t>(noise.next())),
	                          deluge::dsp::Q<31>::from_raw(random_element << 8));
	min_search += scale_shl_wrap<2>(mul(deluge::dsp::Q<31>::from_raw(min_search), fraction)).raw();

	int32_t back_edge = min_search >> kPercBufferReductionMagnitude; // Pixellated
	int32_t how_far_back_searched = 0;
	int32_t perc_pos =
	    (num_raw_samples_processed_at_now_time + kPercBufferReductionSize - 1) >> kPercBufferReductionMagnitude;

	uint32_t total_perc = 0;
	float best_average = 0;
	int32_t best_how_far_back = min_search >> kPercBufferReductionMagnitude; // Pixellated

	while (back_edge < (max_search >> kPercBufferReductionMagnitude)) {

		while (how_far_back_searched < back_edge) {
			how_far_back_searched++;
			if (how_far_back_searched > perc_pos) {
				goto stopPercSearch;
			}
			int32_t perc_here = live_input.percussiveness_at((uint32_t)(perc_pos - how_far_back_searched));
			total_perc += perc_here;
		}

		float average_perc = (float)total_perc / how_far_back_searched;
		if (average_perc > best_average) {
			best_average = average_perc;
			best_how_far_back = how_far_back_searched;
		}

		// else if (averagePerc < bestAverage * 0.5) break; // Didn't help

		back_edge++;
	}

stopPercSearch:

	how_far_back = best_how_far_back << kPercBufferReductionMagnitude;
	return how_far_back;
}

/// @brief What the old hop end's alignment left: the offset and the sub-sample phase.
struct ReferenceAlignment {
	int32_t best_offset = 0;
	int32_t additional_osc_pos = 0;
};

/// @brief The old hop end's alignment, verbatim but for its inputs, from its checks of the history to its last pass.
template <typename Frame>
ReferenceAlignment
reference_align(const dsp::fixed::pitch_shift::InputHistory<Frame>& history, int32_t averages_start_pos_new_head,
                int32_t averages_start_offset_from_head, int32_t length_per_moving_average, int32_t search_size,
                int32_t search_direction, int32_t num_full_directions_searched, int32_t phase_increment,
                const dsp::fixed::pitch_shift::MovingAverages& old_totals, uint64_t num_raw_samples_processed_latest) {
	const HistoryByIndex<Frame> live_input{history};
	int32_t old_head_totals[TimeStretch::Crossfade::kNumMovingAverages];
	std::ranges::copy(old_totals, old_head_totals);
	int32_t best_offset = 0;
	int32_t additional_osc_pos = 0;
	{
		if (((uint32_t)(averages_start_pos_new_head - averages_start_offset_from_head) & (kInputRawBufferSize - 1))
		    >= num_raw_samples_processed_latest) {
			goto stopSearch;
		}

		if (((uint32_t)(num_raw_samples_processed_latest - averages_start_pos_new_head) & (kInputRawBufferSize - 1))
		    < static_cast<uint32_t>(length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)) {
			goto stopSearch;
		}

		int32_t new_head_totals[TimeStretch::Crossfade::kNumMovingAverages];
		std::ranges::copy(live_input.moving_averages(averages_start_pos_new_head, length_per_moving_average),
		                  new_head_totals);

		int32_t best_difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_totals);
		int32_t times_sign_flipped = 0;

		int32_t initial_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_totals);

startSearch:
		int32_t last_total_change = initial_total_change;

		int32_t read_pos[TimeStretch::Crossfade::kNumMovingAverages + 1];

		read_pos[0] = averages_start_pos_new_head;
		if (search_direction == -1) {
			read_pos[0] = (uint32_t)(read_pos[0] - 1) & (kInputRawBufferSize - 1);
		}

		int32_t new_head_running_totals[TimeStretch::Crossfade::kNumMovingAverages];
		for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {
			new_head_running_totals[i] = new_head_totals[i];
			read_pos[i + 1] = (uint32_t)(read_pos[i] + length_per_moving_average) & (kInputRawBufferSize - 1);
		}

		int32_t offset_now = 0;
		int32_t end_offset;

		int32_t search_size_boundary = search_size;
		if (search_direction == -1) {
			if (num_raw_samples_processed_latest < kInputRawBufferSize) { // Is this right?
				search_size_boundary =
				    averages_start_pos_new_head - averages_start_offset_from_head
				    - 1; // The -1 is becacuse of the 1 we subtract from readPos[0] above when searching left
				if (search_size_boundary <= 0) {
					goto searchNextDirection;
				}
			}
		}
		else {
			// readPos is sized [kNumMovingAverages + 1] and filled [0..kNumMovingAverages]
			// (above); indexing [kNumMovingAverages + 1] read one past the end — adjacent
			// stack garbage → wrong search boundary. The furthest filled read position is
			// readPos[kNumMovingAverages].
			search_size_boundary =
			    (uint32_t)(num_raw_samples_processed_latest - read_pos[TimeStretch::Crossfade::kNumMovingAverages])
			    & (kInputRawBufferSize - 1);
		}

		{
			int32_t search_size_here = std::min(search_size, search_size_boundary);
			// guard against potential infinite do/while loop below
			if (search_size_here <= 0) {
				goto searchNextDirection;
			}
			end_offset = search_size_here * search_direction;
		}

		do {

			for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages + 1; i++) {

				int32_t read_value = live_input.folded_high_word(read_pos[i]);

				read_pos[i] = (uint32_t)(read_pos[i] + search_direction) & (kInputRawBufferSize - 1);

				if (i < TimeStretch::Crossfade::kNumMovingAverages) {
					new_head_running_totals[i] -= read_value * search_direction;
				}

				if (i > 0) {
					new_head_running_totals[i - 1] += read_value * search_direction;
				}
			}

			int32_t difference_abs = deluge::dsp::fixed::totalDifferenceAbs(old_head_totals, new_head_running_totals);

			// If our very first read is worse, let's switch search direction right now - that'll improve our odds
			if (offset_now == 0 && search_direction == 1 && !num_full_directions_searched
			    && difference_abs > best_difference_abs) {
				search_direction = -search_direction;
				goto startSearch;
			}

			int32_t new_offset_now = offset_now + search_direction;

			// Keep track of best match
			bool this_offset_is_best_match = (difference_abs < best_difference_abs);
			if (this_offset_is_best_match) {
				best_difference_abs = difference_abs;
				best_offset = new_offset_now;
			}

			int32_t this_total_change = deluge::dsp::fixed::totalChange(old_head_totals, new_head_running_totals);

			// If sign just flipped...
			if ((this_total_change >= 0) != (last_total_change >= 0)) {

				// Try going in between the samples for the most accurate positioning, lining-up-wise.
				// The benefit of this is visible on a spectrum analysis if you're pitching a high-pitched sine wave
				// right down, while also time stretching it
				if (phase_increment != kMaxSampleValue
				    && (this_offset_is_best_match || offset_now == best_offset)) { // If best was this one or last one
					uint32_t this_total_difference_abs = std::abs(this_total_change);
					uint32_t last_total_difference_abs = std::abs(last_total_change);
					additional_osc_pos = ((uint64_t)last_total_difference_abs << 24)
					                     / (uint32_t)(last_total_difference_abs + this_total_difference_abs);
					if (search_direction == -1) {
						additional_osc_pos = kMaxSampleValue - additional_osc_pos;
					}
					if (this_offset_is_best_match != (search_direction == -1)) {
						best_offset--;
					}
				}

				// After sign has flipped twice (in total, including both search directions), we can be fairly sure
				// we've found a good fit
				times_sign_flipped++;
				if (times_sign_flipped >= 2) {
					goto stopSearch;
				}
			}

			offset_now = new_offset_now;
			last_total_change = this_total_change;
		} while (offset_now != end_offset);

searchNextDirection:
		// Search the other direction if we haven't already
		num_full_directions_searched++;
		if (num_full_directions_searched < 2) {
			search_direction = -search_direction;
			goto startSearch;
		}
	}
stopSearch:
	return {.best_offset = best_offset, .additional_osc_pos = additional_osc_pos};
}

} // namespace deluge::synth::reference_hop
// NOLINTEND
