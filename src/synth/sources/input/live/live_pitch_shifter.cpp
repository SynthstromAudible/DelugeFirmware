/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "synth/sources/input/live/live_pitch_shifter.h"
#include "definitions_cxx.hpp" // TimeStretch::Crossfade constants
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"
#include "dsp-next/fixed/timestretch/crossfade.h" // totalDifferenceAbs / totalChange
#include "foundation/panic.h"                     // FREEZE_WITH_ERROR
#include "memory/memory_allocator_interface.h"    // allocMaxSpeed / delugeDealloc (the thin allocator seam)
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/legacy_input_view.h"
#include <algorithm> // std::min / std::max / std::clamp
#include <cstdlib>
#include <memory>
#include <new>
#include <span>

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(bugprone-implicit-widening-of-multiplication-result,cppcoreguidelines-avoid-goto,bugprone-narrowing-conversions,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-init-variables,cppcoreguidelines-narrowing-conversions,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-arrays,modernize-avoid-c-style-cast,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-make-member-function-const,readability-redundant-casting,readability-use-std-min-max)

namespace deluge::synth {

template <typename Frame>
LivePitchShifter<Frame>::LivePitchShifter(InputChannel new_channel, int32_t phase_increment) {
	channel = new_channel;

	if (phase_increment < kMaxSampleValue) {
		next_crossfade_length = samples_til_hop_end = kInterpolationMaxNumSamples * 2;
	}

	else if (phase_increment < 17774841) { // If up by less than 1 semitone...
		samples_til_hop_end = 2048;
		next_crossfade_length = 256;
	}

	else {
		next_crossfade_length = samples_til_hop_end = 256;
	}

	crossfade_progress = kMaxSampleValue;
	samples_into_hop = 0;

	// The newer head starts direct, at the ring's start (quirk Q1,
	// docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md)
	play_heads[PLAY_HEAD_NEWER].template on<&Kernel::note_percussiveness_entry>(
	    dsp::fixed::pitch_shift::kNoPercussivenessEntry);
}

template <typename Frame>
int32_t LivePitchShifter<Frame>::render(dsp::Q<31>* __restrict__ output_buffer, int32_t num_samples_this_function_call,
                                        int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
                                        int32_t interpolation_buffer_size, AudioInputPort& port,
                                        dsp::fixed::Cong& noise) {

	const LegacyInputView live_input = LegacyInputView::of(port, channel, false);
	if (!live_input) {
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("E165");
		}
		return 0; // Render nothing rather than dereference null
	}

	live_input.ingest(num_samples_this_function_call, port);

	int32_t which_kernel = dsp::fixed::WindowedSincKernel::variant_for(phase_increment);
	const History* history = live_input.template history<Frame>();
	for (Head& head : play_heads) {
		configure_head(head, history, phase_increment, which_kernel, interpolation_buffer_size);
	}

	uint64_t num_raw_samples_processed_at_start = live_input.frames_written() - num_samples_this_function_call;

	bool just_did_hop = false;
	int32_t hops_ended = 0;

startRenderAgain:

	if (!just_did_hop && phase_increment > kMaxSampleValue) {
		int32_t max_playable_samples_newer =
		    get_estimated_playtime_remaining(play_heads[PLAY_HEAD_NEWER], live_input, phase_increment);

		if (samples_til_hop_end + next_crossfade_length > max_playable_samples_newer) {

			int32_t max_total_playable = max_playable_samples_newer + samples_into_hop;
			next_crossfade_length = std::min(next_crossfade_length, max_total_playable >> 1);

			samples_til_hop_end = max_playable_samples_newer - next_crossfade_length;
			// D_PRINTLN("shortening hop");

			if (samples_til_hop_end < 0) {
				samples_til_hop_end = 0;
				next_crossfade_length = std::max(max_playable_samples_newer, 0_i32);
				// D_PRINTLN("nex");
				crossfade_progress = kMaxSampleValue;
			}

			else if (samples_til_hop_end == 0) {
				// D_PRINTLN("to zero");
			}

			else if (samples_til_hop_end > 0 && older_play_head_is_currently_sounding()) {
				uint32_t min_crossfade_increment =
				    (uint32_t)(kMaxSampleValue - crossfade_progress) / samples_til_hop_end + 1;
				if (min_crossfade_increment > crossfade_increment) {
					crossfade_increment = min_crossfade_increment;
					// D_PRINTLN("d");
				}
			}
		}

		if (samples_til_hop_end && older_play_head_is_currently_sounding()) {
			uint32_t max_playable_samples_older =
			    get_estimated_playtime_remaining(play_heads[PLAY_HEAD_OLDER], live_input, phase_increment);
			// D_PRINTLN(maxPlayableSamplesOlder);
			if (!max_playable_samples_older) {
				crossfade_increment = kMaxSampleValue;
			}
			else {
				uint32_t min_crossfade_increment =
				    (uint32_t)(kMaxSampleValue - crossfade_progress) / max_playable_samples_older + 1;
				// crossfadeIncrement = std::max(crossfadeIncrement, minCrossfadeIncrement);
				if (min_crossfade_increment > crossfade_increment) {
					crossfade_increment = min_crossfade_increment;
					// D_PRINTLN("c");
				}
			}
		}
	}

	// If the percussiveness is higher at "now" time than at the newer play head, end the hop.
	// This was designed with pitching-down in mind, but sounds good on pitching-up too
	if (!just_did_hop && !older_play_head_is_currently_sounding() && samples_til_hop_end
	    && play_heads[PLAY_HEAD_NEWER].state().kind != dsp::fixed::pitch_shift::HeadKind::DIRECT) {

		int32_t how_far_back = get_num_raw_samples_behind_input(play_heads[PLAY_HEAD_NEWER], live_input);

		uint32_t newer_play_head_perc_pos =
		    (live_input.frames_written() - how_far_back - 1) >> kPercBufferReductionMagnitude;

		uint32_t latest_perc_pos_before = (num_raw_samples_processed_at_start - 1) >> kPercBufferReductionMagnitude;
		uint32_t latest_perc_pos_now = (live_input.frames_written() - 1) >> kPercBufferReductionMagnitude;

		if (latest_perc_pos_now != newer_play_head_perc_pos
		    && (newer_play_head_perc_pos != play_heads[PLAY_HEAD_NEWER].state().percussiveness_entry
		        || latest_perc_pos_now != latest_perc_pos_before)) {

			int32_t perc_latest = live_input.percussiveness_at(latest_perc_pos_now);
			int32_t perc_newer_play_head = live_input.percussiveness_at(newer_play_head_perc_pos);

			if (perc_latest >= perc_newer_play_head + perc_threshold_for_cut) {
				/*
				D_PRINT(percLatest);
				D_PRINTLN(" vs  %d", percNewerPlayHead);
				*/
				samples_til_hop_end = 0;
			}
		}

		play_heads[PLAY_HEAD_NEWER].template on<&Kernel::note_percussiveness_entry>(newer_play_head_perc_pos);
	}

	if (!samples_til_hop_end) {
		hop_end(phase_increment, live_input, num_raw_samples_processed_at_start, live_input.frames_written(), noise);
		++hops_ended;
		just_did_hop = true;
		goto startRenderAgain;
	}

	int32_t num_samples_this_timestretched_read =
	    std::min((int32_t)num_samples_this_function_call, samples_til_hop_end);

	bool older_play_head_audible_here = older_play_head_is_currently_sounding();

	int32_t newer_source_amplitude_now;
	int32_t newer_amplitude_increment_now;
	int32_t older_source_amplitude_now;
	int32_t older_amplitude_increment_now;

	// If the older play-head is still fading out, which means we need to work out the crossfade envelope too...
	if (older_play_head_audible_here) {

		// Use linear crossfades. These sound less jarring when doing short hops. Square root ones sound better for
		// longer hops, with more difference in material between hops (i.e. time more stretched)
		using deluge::dsp::Q;
		// The crossfade's Q24 progress as the newer hop's Q31 amplitude, the older's the rest of full scale
		int32_t newer_hop_amplitude_now = as_wrap<Q<31>>(Q<24>::from_raw(crossfade_progress)).raw();
		int32_t older_hop_amplitude_now = 2147483647 - newer_hop_amplitude_now;

		crossfade_progress += crossfade_increment * num_samples_this_timestretched_read;

		// Where the progress will be, held to [−1, 1) before its move to Q31
		int32_t newer_hop_amplitude_after = as_wrap<Q<31>>(clamp_bits<25>(Q<24>::from_raw(crossfade_progress))).raw();

		int32_t newer_hop_amplitude_increment = (int32_t)(newer_hop_amplitude_after - newer_hop_amplitude_now)
		                                        / (int32_t)num_samples_this_timestretched_read;

		// The source amplitude times each Q31 hop amplitude (and its step), moved back to Q31
		const auto source = Q<31>::from_raw(amplitude);
		int32_t hop_amplitude_change = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(newer_hop_amplitude_increment)).raw();

		newer_amplitude_increment_now = amplitude_increment + hop_amplitude_change;
		newer_source_amplitude_now = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(newer_hop_amplitude_now)).raw();

		older_amplitude_increment_now = amplitude_increment - hop_amplitude_change;
		older_source_amplitude_now = mul_as_wrap<Q<31>>(source, Q<31>::from_raw(older_hop_amplitude_now)).raw();
	}

	// Or, if we're just hearing the newer play-head
	else {
		newer_source_amplitude_now = amplitude;
		newer_amplitude_increment_now = amplitude_increment;
		// No need to set the "older" ones, cos that's not going to be rendered
	}

	// The segment's frames, in place: each head adds into them
	const std::span<Frame> segment = frames_of(output_buffer, num_samples_this_timestretched_read);
	const auto render_head = [&](Head& head, int32_t head_amplitude, int32_t head_amplitude_increment) {
		configure_head(head, history, phase_increment, which_kernel, interpolation_buffer_size);
		// The ramp's words, as the crossfade computed them
		head.template on<&Kernel::start_segment>(dsp::Q<31>::from_raw(head_amplitude),
		                                         dsp::Q<31>::from_raw(head_amplitude_increment));
		head.render_block(segment, segment);
	};

	// Read newer play-head
	render_head(play_heads[PLAY_HEAD_NEWER], newer_source_amplitude_now, newer_amplitude_increment_now);

	// Read older play-head
	if (older_play_head_audible_here) {
		render_head(play_heads[PLAY_HEAD_OLDER], older_source_amplitude_now, older_amplitude_increment_now);
	}

	samples_til_hop_end -= num_samples_this_timestretched_read;
	samples_into_hop += num_samples_this_timestretched_read;
	num_samples_this_function_call -= num_samples_this_timestretched_read;

	if (num_samples_this_function_call) {
		output_buffer += num_samples_this_timestretched_read * kNumChannels;

		amplitude += amplitude_increment * num_samples_this_timestretched_read;

		num_raw_samples_processed_at_start += num_samples_this_timestretched_read;

		goto startRenderAgain;
	}

	return hops_ended;
}

template <typename Frame>
void LivePitchShifter<Frame>::hop_end(int32_t phase_increment, const LegacyInputView& live_input,
                                      uint64_t num_raw_samples_processed_at_now_time,
                                      uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise) {

	// int32_t numChannelsNow = numChannels;

	// D_PRINTLN("");
	if (crossfade_progress < kMaxSampleValue) {
		// if (ALPHA_OR_BETA_VERSION) FREEZE_WITH_ERROR("FADE");
	}
	// D_PRINTLN(phaseIncrement);

	// D_PRINTLN((uint32_t)(liveInputBuffer->numRawSamplesProcessed - playHeads[PLAY_HEAD_NEWER].rawBufferReadPos) &
	// (kInputRawBufferSize - 1)); // How far behind raw buffer is being read

	// What was new is now old
	play_heads[PLAY_HEAD_OLDER].template on<&Kernel::take>(play_heads[PLAY_HEAD_NEWER].state());
	uint32_t newer_read_pos = play_heads[PLAY_HEAD_NEWER].state().cursor.index();
	uint32_t this_crossfade_length = next_crossfade_length;

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
	    (uint32_t)(num_raw_samples_processed_latest - play_heads[PLAY_HEAD_OLDER].state().cursor.index())
	    & (kInputRawBufferSize - 1);

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
		    (play_heads[PLAY_HEAD_OLDER].state().cursor.index() + averages_start_offset_from_head)
		    & (kInputRawBufferSize - 1);
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
	if (length_per_moving_average
	    && play_heads[PLAY_HEAD_OLDER].state().kind != dsp::fixed::pitch_shift::HeadKind::DIRECT) {

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

	additional_osc_pos += play_heads[PLAY_HEAD_OLDER].state().phase;
	if (additional_osc_pos >= kMaxSampleValue) {
		additional_osc_pos -= kMaxSampleValue;
		best_offset++;
	}

	newer_read_pos = (uint32_t)(newer_read_pos + best_offset) & (kInputRawBufferSize - 1);

	// If still here, use raw direct buffer - usually because we're pitching down

	using Hop = Kernel::Hop;
	if (phase_increment == kMaxSampleValue) {
		play_heads[PLAY_HEAD_NEWER].template on<&Kernel::begin_hop>(
		    Hop{.cursor =
		            dsp::fixed::pitch_shift::RingPosition(static_cast<uint32_t>(num_raw_samples_processed_at_now_time)),
		        .kind = dsp::fixed::pitch_shift::HeadKind::DIRECT});
	}

	else {
		play_heads[PLAY_HEAD_NEWER].template on<&Kernel::begin_hop>(
		    Hop{.cursor = dsp::fixed::pitch_shift::RingPosition(newer_read_pos),
		        .phase = static_cast<uint32_t>(additional_osc_pos),
		        .kind = dsp::fixed::pitch_shift::HeadKind::REPITCHING});
	}

	if (this_crossfade_length) {
		crossfade_progress = 0;
		crossfade_increment = (kMaxSampleValue - 1) / this_crossfade_length + 1;
	}
	else {
		crossfade_progress = kMaxSampleValue;
	}

	/*
	if (phaseIncrement > kMaxSampleValue) {
	    uint64_t totalPlayableSamples = ((uint64_t)howFarBack << 24) / (uint32_t)(phaseIncrement - kMaxSampleValue);
	    totalPlayableSamples = std::max(totalPlayableSamples, (uint64_t)2);

	    totalPlayableSamples = std::min(totalPlayableSamples, (uint64_t)16384);

	    if (nextCrossfadeLength > totalPlayableSamples - 1) nextCrossfadeLength = totalPlayableSamples - 1;

	    samplesTilHopEnd = totalPlayableSamples - nextCrossfadeLength;
	}
	*/

	samples_into_hop = 0;
}

template <typename Frame>
std::span<Frame> LivePitchShifter<Frame>::frames_of(dsp::Q<31>* output_buffer, int32_t num_samples) {
	if constexpr (std::is_same_v<Frame, dsp::StereoSample>) {
		return dsp::as_frames(std::span<dsp::Q<31>>{output_buffer, static_cast<std::size_t>(num_samples) * 2});
	}
	else {
		return {output_buffer, static_cast<std::size_t>(num_samples)};
	}
}

template <typename Frame>
void LivePitchShifter<Frame>::configure_head(Head& head, const History* history, int32_t phase_increment,
                                             int32_t which_kernel, int32_t interpolation_buffer_size) {
	head.configure({.history = history,
	                .phase_increment = static_cast<uint32_t>(phase_increment),
	                .kernel_variant = which_kernel,
	                .kind = head.state().kind,
	                .linear = interpolation_buffer_size <= 2});
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio. Only valid if phaseIncrement > kMaxSampleValue
template <typename Frame>
int32_t LivePitchShifter<Frame>::get_estimated_playtime_remaining(const Head& head, const LegacyInputView& live_input,
                                                                  int32_t phase_increment) {
	if (head.state().kind != dsp::fixed::pitch_shift::HeadKind::REPITCHING) {
		return 2147483647; // A direct head never runs out
	}
	uint32_t how_far_back_raw = (live_input.frames_written() - head.state().cursor.index()) & (kInputRawBufferSize - 1);
	// The distance divided by the rate and then by the rate's excess over 1 (quirk Q2,
	// docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md)
	uint32_t how_far_back = (static_cast<uint64_t>(how_far_back_raw) << 24) / static_cast<uint32_t>(phase_increment);
	uint64_t estimate =
	    (static_cast<uint64_t>(how_far_back) << 24) / static_cast<uint32_t>(phase_increment - kMaxSampleValue);
	if (estimate >= 2147483647) {
		return 2147483647;
	}
	return static_cast<int32_t>(estimate);
}

template <typename Frame>
int32_t LivePitchShifter<Frame>::get_num_raw_samples_behind_input(const Head& head, const LegacyInputView& live_input) {
	if (head.state().kind != dsp::fixed::pitch_shift::HeadKind::REPITCHING) {
		return 0;
	}
	return static_cast<int32_t>((live_input.frames_written() - head.state().cursor.index())
	                            & (kInputRawBufferSize - 1));
}

template <typename Frame>
bool LivePitchShifter<Frame>::older_play_head_is_currently_sounding() {
	return (crossfade_progress < kMaxSampleValue);
}

template <typename Frame>
bool LivePitchShifter<Frame>::may_be_removed_without_click() {
	return (!older_play_head_is_currently_sounding()
	        && play_heads[PLAY_HEAD_NEWER].state().kind == dsp::fixed::pitch_shift::HeadKind::DIRECT);
}

template class LivePitchShifter<dsp::Q<31>>;
template class LivePitchShifter<dsp::StereoSample>;

bool PitchShifter::create(InputChannel channel, int32_t phase_increment) {
	destroy();
	stereo_ = channel == InputChannel::STEREO;
	void* memory =
	    allocMaxSpeed(stereo_ ? sizeof(LivePitchShifter<dsp::StereoSample>) : sizeof(LivePitchShifter<dsp::Q<31>>));
	if (memory == nullptr) {
		return false;
	}
	// Placed in the allocator's block, which destroy frees
	if (stereo_) {
		new (memory)
		    LivePitchShifter<dsp::StereoSample>(channel, phase_increment); // NOLINT(cppcoreguidelines-owning-memory)
	}
	else {
		new (memory) LivePitchShifter<dsp::Q<31>>(channel, phase_increment); // NOLINT(cppcoreguidelines-owning-memory)
	}
	block_ = memory;
	return true;
}

void PitchShifter::destroy() {
	if (block_ == nullptr) {
		return;
	}
	if (stereo_) {
		std::destroy_at(static_cast<LivePitchShifter<dsp::StereoSample>*>(block_));
	}
	else {
		std::destroy_at(static_cast<LivePitchShifter<dsp::Q<31>>*>(block_));
	}
	delugeDealloc(block_);
	block_ = nullptr;
}

int32_t PitchShifter::render(dsp::Q<31>* output_buffer, int32_t num_samples, int32_t phase_increment, int32_t amplitude,
                             int32_t amplitude_increment, int32_t interpolation_buffer_size, AudioInputPort& port,
                             dsp::fixed::Cong& noise) {
	if (stereo_) {
		return static_cast<LivePitchShifter<dsp::StereoSample>*>(block_)->render(
		    output_buffer, num_samples, phase_increment, amplitude, amplitude_increment, interpolation_buffer_size,
		    port, noise);
	}
	return static_cast<LivePitchShifter<dsp::Q<31>>*>(block_)->render(output_buffer, num_samples, phase_increment,
	                                                                  amplitude, amplitude_increment,
	                                                                  interpolation_buffer_size, port, noise);
}

bool PitchShifter::may_be_removed_without_click() {
	if (stereo_) {
		return static_cast<LivePitchShifter<dsp::StereoSample>*>(block_)->may_be_removed_without_click();
	}
	return static_cast<LivePitchShifter<dsp::Q<31>>*>(block_)->may_be_removed_without_click();
}

} // namespace deluge::synth
// NOLINTEND(bugprone-implicit-widening-of-multiplication-result,cppcoreguidelines-avoid-goto,bugprone-narrowing-conversions,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-init-variables,cppcoreguidelines-narrowing-conversions,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-arrays,modernize-avoid-c-style-cast,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-make-member-function-const,readability-redundant-casting,readability-use-std-min-max)
