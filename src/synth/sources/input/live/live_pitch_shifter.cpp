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
#include "definitions_cxx.hpp"     // TimeStretch::Crossfade constants
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp-next/fixed/timestretch/crossfade.h" // totalDifferenceAbs / totalChange
#include "foundation/panic.h"                     // FREEZE_WITH_ERROR
#include "synth/sources/input/audio_input_port.h"
#include "synth/sources/input/live/live_input_buffer.h"
#include <algorithm> // std::min / std::max / std::clamp
#include <cstdlib>

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(bugprone-implicit-widening-of-multiplication-result,bugprone-narrowing-conversions,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-init-variables,cppcoreguidelines-narrowing-conversions,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-arrays,modernize-avoid-c-style-cast,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-make-member-function-const,readability-redundant-casting,readability-use-std-min-max)

// #define MEASURE_HOP_END_PERFORMANCE 1
#ifndef MEASURE_HOP_END_PERFORMANCE
#define MEASURE_HOP_END_PERFORMANCE 0 // NOLINT(cppcoreguidelines-macro-usage): #if tests it
#endif

namespace deluge::synth {

LivePitchShifter::LivePitchShifter(InputChannel new_channel, int32_t phase_increment) {
	channel = new_channel;
	num_channels = (new_channel == InputChannel::STEREO) ? 2 : 1;

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

#if INPUT_ENABLE_REPITCHED_BUFFER
	stillWritingToRepitchedBuffer = false;
	repitchedBuffer = NULL;
#endif
	consider_repitched_buffer(phase_increment);

	play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_DIRECT;
	play_heads[PLAY_HEAD_NEWER].osc_pos = 0;

	play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos = 0;
	play_heads[PLAY_HEAD_NEWER].perc_pos = 0xFFFFFFFF;
}

LivePitchShifter::~LivePitchShifter() {

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer) {
		delugeDealloc(repitchedBuffer);
	}
#endif
}

int32_t LivePitchShifter::render(int32_t* __restrict__ output_buffer, int32_t num_samples_this_function_call,
                                 int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
                                 int32_t interpolation_buffer_size, AudioInputPort& port, dsp::fixed::Cong& noise) {

	LiveInputBuffer* live_input_buffer = port.buffer_for_channel(channel, false);
	if (!live_input_buffer) {
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("E165");
		}
		return 0; // Render nothing rather than dereference null
	}

	live_input_buffer->give_input(num_samples_this_function_call, port, channel);

	int32_t which_kernel = dsp::fixed::WindowedSincKernel::variant_for(phase_increment);

	uint64_t num_raw_samples_processed_at_start =
	    live_input_buffer->num_raw_samples_processed - num_samples_this_function_call;

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer) {
		for (int32_t i = 0; i < numSamplesThisFunctionCall; i++) {

			// Shift contents of interpolation buffer along by 1
			for (int32_t b = kInterpolationMaxNumSamples - 1; b >= 1; b--) {
				interpolationBuffer[0][b] = interpolationBuffer[0][b - 1];
				if (numChannels == 2) {
					interpolationBuffer[1][b] = interpolationBuffer[1][b - 1];
				}
			}

			// Feed this 1 input sample into interpolation buffer
			StereoSample* inputSample = audioDriver.getInputSample(i);

			interpolationBuffer[0][0] = (channel == InputChannel::RIGHT) ? inputSample->r : inputSample->l;

			if (numChannels == 2) {
				interpolationBuffer[1][0] = inputSample->r;
			}

			while (oscPos < kMaxSampleValue) {

				// Interpolate and put in buffer
				interpolate(&repitchedBuffer[repitchedBufferWritePos * numChannels], interpolationBufferSize,
				            numChannels, whichKernel);

				repitchedBufferWritePos = (repitchedBufferWritePos + 1) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
				repitchedBufferNumSamplesWritten++;

				oscPos += phaseIncrement;
			}

			oscPos -= kMaxSampleValue;
		}
	}
#endif

	bool just_did_hop = false;
	int32_t hops_ended = 0;

startRenderAgain:

	if (!just_did_hop && phase_increment > kMaxSampleValue) {
		int32_t max_playable_samples_newer = play_heads[PLAY_HEAD_NEWER].get_estimated_playtime_remaining(
#if INPUT_ENABLE_REPITCHED_BUFFER
		    repitchedBufferNumSamplesWritten,
#else
		    0,
#endif
		    live_input_buffer, phase_increment);

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
			uint32_t max_playable_samples_older = play_heads[PLAY_HEAD_OLDER].get_estimated_playtime_remaining(
#if INPUT_ENABLE_REPITCHED_BUFFER
			    repitchedBufferNumSamplesWritten,
#else
			    0,
#endif
			    live_input_buffer, phase_increment);
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

#if !MEASURE_HOP_END_PERFORMANCE
	// If the percussiveness is higher at "now" time than at the newer play head, end the hop.
	// This was designed with pitching-down in mind, but sounds good on pitching-up too
	if (!just_did_hop && !older_play_head_is_currently_sounding() && samples_til_hop_end
	    && play_heads[PLAY_HEAD_NEWER].mode != PlayHeadMode::RAW_DIRECT) {

		int32_t how_far_back =
		    play_heads[PLAY_HEAD_NEWER].get_num_raw_samples_behind_input(live_input_buffer, this, phase_increment);

		uint32_t newer_play_head_perc_pos =
		    (live_input_buffer->num_raw_samples_processed - how_far_back - 1) >> kPercBufferReductionMagnitude;

		uint32_t latest_perc_pos_before = (num_raw_samples_processed_at_start - 1) >> kPercBufferReductionMagnitude;
		uint32_t latest_perc_pos_now =
		    (live_input_buffer->num_raw_samples_processed - 1) >> kPercBufferReductionMagnitude;

		if (latest_perc_pos_now != newer_play_head_perc_pos
		    && (newer_play_head_perc_pos != play_heads[PLAY_HEAD_NEWER].perc_pos
		        || latest_perc_pos_now != latest_perc_pos_before)) {

			int32_t perc_latest = live_input_buffer->perc_buffer[latest_perc_pos_now & (kInputPercBufferSize - 1)];
			int32_t perc_newer_play_head =
			    live_input_buffer->perc_buffer[newer_play_head_perc_pos & (kInputPercBufferSize - 1)];

			if (perc_latest >= perc_newer_play_head + perc_threshold_for_cut) {
				/*
				D_PRINT(percLatest);
				D_PRINTLN(" vs  %d", percNewerPlayHead);
				*/
				samples_til_hop_end = 0;
			}
		}

		play_heads[PLAY_HEAD_NEWER].perc_pos = newer_play_head_perc_pos;
	}
#endif

	if (!samples_til_hop_end) {
		hop_end(phase_increment, live_input_buffer, num_raw_samples_processed_at_start,
		        live_input_buffer->num_raw_samples_processed, noise);
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
		int32_t newer_hop_amplitude_now = crossfade_progress << 7;
		int32_t older_hop_amplitude_now = 2147483647 - newer_hop_amplitude_now;

		crossfade_progress += crossfade_increment * num_samples_this_timestretched_read;

		int32_t newer_hop_amplitude_after = lshift_and_saturate<7>(crossfade_progress);

		int32_t newer_hop_amplitude_increment = (int32_t)(newer_hop_amplitude_after - newer_hop_amplitude_now)
		                                        / (int32_t)num_samples_this_timestretched_read;

		int32_t hop_amplitude_change = multiply_32x32_rshift32(amplitude, newer_hop_amplitude_increment) << 1;

		newer_amplitude_increment_now = amplitude_increment + hop_amplitude_change;
		newer_source_amplitude_now = multiply_32x32_rshift32(amplitude, newer_hop_amplitude_now) << 1;

		older_amplitude_increment_now = amplitude_increment - hop_amplitude_change;
		older_source_amplitude_now = multiply_32x32_rshift32(amplitude, older_hop_amplitude_now) << 1;
	}

	// Or, if we're just hearing the newer play-head
	else {
		newer_source_amplitude_now = amplitude;
		newer_amplitude_increment_now = amplitude_increment;
		// No need to set the "older" ones, cos that's not going to be rendered
	}

	// Read newer play-head
	play_heads[PLAY_HEAD_NEWER].render(output_buffer, num_samples_this_timestretched_read, num_channels,
	                                   phase_increment, newer_source_amplitude_now, newer_amplitude_increment_now,
#if INPUT_ENABLE_REPITCHED_BUFFER
	                                   repitchedBuffer,
#else
	                                   nullptr,
#endif
	                                   live_input_buffer->raw_buffer, which_kernel, interpolation_buffer_size);

	// Read older play-head
	if (older_play_head_audible_here) {
		play_heads[PLAY_HEAD_OLDER].render(output_buffer, num_samples_this_timestretched_read, num_channels,
		                                   phase_increment, older_source_amplitude_now, older_amplitude_increment_now,
#if INPUT_ENABLE_REPITCHED_BUFFER
		                                   repitchedBuffer,
#else
		                                   nullptr,
#endif
		                                   live_input_buffer->raw_buffer, which_kernel, interpolation_buffer_size);
	}

	samples_til_hop_end -= num_samples_this_timestretched_read;
	samples_into_hop += num_samples_this_timestretched_read;
	num_samples_this_function_call -= num_samples_this_timestretched_read;

	if (num_samples_this_function_call) {
		output_buffer += num_samples_this_timestretched_read * num_channels;

		amplitude += amplitude_increment * num_samples_this_timestretched_read;

		num_raw_samples_processed_at_start += num_samples_this_timestretched_read;

		goto startRenderAgain;
	}

	return hops_ended;
}

const int16_t kMinSearchFine[] = {
    8, 8, 8, 8, 8, 8,  8,  8,  // -12, ....
    8, 8, 8, 8, 8, 12, 14, 16, // +0, ....
    17                         // +12
};

const int16_t kMinSearchCoarse[] = {10, 10, 10, 17, 20};

const int16_t kMaxSearchFine[] = {
    8, 8, 8,  8,  8,  8,  8,  8,  // -12, ....
    8, 8, 10, 11, 12, 14, 16, 18, // +0, ....
    21                            // +12
};

const int16_t kMaxSearchCoarse[] = {15, 15, 15, 21, 20};

const int16_t kPercThresholdFine[] = {
    18,  18, 18, 18, 24, 30, 25, 35, // -12, ....
    130, 40, 40, 45, 50, 47, 45, 42, // +0, ....
    40                               // +12
};

const int16_t kPercThresholdCoarse[] = {15, 18, 130, 40, 20};

const int16_t kCrossfadeFine[] = {
    30, 31, 32, 34, 35, 30, 25, 10, // -12, ....
    10, 15, 15, 22, 30, 16, 15, 13, // +0, ....
    40                              // +12
};

const int16_t kCrossfadeCoarse[] = {30, 30, 10, 40, 20};

const int16_t kMaxHopLengthFine[] = {
    20,  27,  35,  42,  50,  60,  70,  90,  // -12, ....
    140, 140, 140, 140, 140, 140, 140, 140, // +0, ....
    140                                     // +12
};

const int16_t kMaxHopLengthCoarse[] = {10, 20, 140, 140, 140};

const int16_t kRandomFine[] = {
    0, 0,  0,  0,  0,  0,  0,  0,  // -12, ....
    0, 15, 25, 76, 50, 57, 65, 72, // +0, ....
    80                             // +12
};

const int16_t kRandomCoarse[] = {0, 0, 0, 80, 80};

void LivePitchShifter::hop_end(int32_t phase_increment, LiveInputBuffer* live_input_buffer,
                               uint64_t num_raw_samples_processed_at_now_time,
                               uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise) {

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t startTime = MTU2.TCNT_0;
#endif

	// int32_t numChannelsNow = numChannels;

	// D_PRINTLN("");
	if (crossfade_progress < kMaxSampleValue) {
		// if (ALPHA_OR_BETA_VERSION) FREEZE_WITH_ERROR("FADE");
	}
	// D_PRINTLN(phaseIncrement);

	// D_PRINTLN((uint32_t)(liveInputBuffer->numRawSamplesProcessed - playHeads[PLAY_HEAD_NEWER].rawBufferReadPos) &
	// (kInputRawBufferSize - 1)); // How far behind raw buffer is being read

	// What was new is now old
	play_heads[PLAY_HEAD_OLDER] = play_heads[PLAY_HEAD_NEWER];
	uint32_t this_crossfade_length = next_crossfade_length;

	int32_t pitch_log = dsp::fixed::quick_log(phase_increment);

	int32_t min_search;
	int32_t max_search;
	int32_t max_hop_length;
	int32_t random_element;

	// Neutral is (832 << 20). Each octave is a (32 << 20)

	// If within +/- 1 octave...
	if (pitch_log >= (800 << 20) && pitch_log < (864 << 20)) {
		int32_t position = pitch_log - (800 << 20);

		min_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMinSearchFine, 4) >> 9;
		max_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMaxSearchFine, 4) >> 9;
		perc_threshold_for_cut =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kPercThresholdFine, 4) >> 16;
		next_crossfade_length =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kCrossfadeFine, 4) >> 12;
		max_hop_length =
		    (dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kMaxHopLengthFine, 4) >> 16) * 100;
		random_element = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 26, kRandomFine, 4);
	}

	// Or if outside of that...
	else {
		if (pitch_log > (896 << 20)) {
			pitch_log = (896 << 20);
		}
		else if (pitch_log < (768 << 20)) {
			pitch_log = (768 << 20);
		}

		int32_t position = pitch_log - (768 << 20);

		min_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMinSearchCoarse, 2) >> 9;
		max_search = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMaxSearchCoarse, 2) >> 9;
		perc_threshold_for_cut =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kPercThresholdCoarse, 2) >> 16;
		next_crossfade_length =
		    dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kCrossfadeCoarse, 2) >> 12;
		max_hop_length =
		    (dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kMaxHopLengthCoarse, 2) >> 16) * 100;
		random_element = dsp::fixed::kLinearTableInterpolator.interpolate_signed(position, 27, kRandomCoarse, 2);
	}

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
	    (uint32_t)(num_raw_samples_processed_latest - play_heads[PLAY_HEAD_OLDER].raw_buffer_read_pos)
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
		    (play_heads[PLAY_HEAD_OLDER].raw_buffer_read_pos + averages_start_offset_from_head)
		    & (kInputRawBufferSize - 1);
		live_input_buffer->get_averages_for_crossfade(old_head_totals, averages_start_pos_old_head,
		                                              length_per_moving_average, num_channels);
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

		if (live_input_buffer) {

			// Search backwards, looking for the area (ending at now-time) with the highest average percussiveness.
			// This particularly helps to keep the hops from all happening at the same length, which leads to a clearly
			// audible sort of "tone". But, some sort of random element could actually work just as well - I haven't
			// tried yet.

#if !MEASURE_HOP_END_PERFORMANCE
			min_search +=
			    (multiply_32x32_rshift32(
			         min_search, multiply_32x32_rshift32(static_cast<int32_t>(noise.next()), random_element << 8))
			     << 2);
#endif

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
					int32_t perc_here =
					    live_input_buffer
					        ->perc_buffer[(uint32_t)(perc_pos - how_far_back_searched) & (kInputPercBufferSize - 1)];
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
#if !MEASURE_HOP_END_PERFORMANCE
			search_size = std::min(search_size, samples_til_hop_end);
#endif
			num_full_directions_searched = 0;
			search_direction = 1;
		}

		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    (uint32_t)(num_raw_samples_processed_at_now_time - how_far_back) & (kInputRawBufferSize - 1);
	}

	// Or if pitch shifting down...
	else {
		samples_til_hop_end = max_hop_length;

		if (length_per_moving_average) {
			averages_start_pos_new_head = (num_raw_samples_processed_latest
			                               - length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
			                              & (kInputRawBufferSize - 1);
			search_size = 980; // Allow tracking down to about 45Hz
#if !MEASURE_HOP_END_PERFORMANCE
			search_size = std::min(search_size, samples_into_hop);
#endif
			num_full_directions_searched = 1;
			search_direction = -1;
		}

		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    (uint32_t)(num_raw_samples_processed_latest
		               - (length_per_moving_average * TimeStretch::Crossfade::kNumMovingAverages)
		               - averages_start_offset_from_head)
		    & (kInputRawBufferSize - 1);
	}

	int32_t best_offset = 0;

	int32_t additional_osc_pos = 0;

	// If we're all good to do the moving averages...
	if (length_per_moving_average && play_heads[PLAY_HEAD_OLDER].mode != PlayHeadMode::RAW_DIRECT) {

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
		live_input_buffer->get_averages_for_crossfade(new_head_totals, averages_start_pos_new_head,
		                                              length_per_moving_average, num_channels);

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

				int32_t read_value = live_input_buffer->raw_buffer[read_pos[i] * num_channels] >> 16;
				if (num_channels == 2) {
					read_value += live_input_buffer->raw_buffer[read_pos[i] * 2 + 1] >> 16;
				}

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
#if !MEASURE_HOP_END_PERFORMANCE
				if (times_sign_flipped >= 2) {
					goto stopSearch;
				}
#endif
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

	additional_osc_pos += play_heads[PLAY_HEAD_OLDER].osc_pos;
	if (additional_osc_pos >= kMaxSampleValue) {
		additional_osc_pos -= kMaxSampleValue;
		best_offset++;
	}

	play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
	    (uint32_t)(play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos + best_offset) & (kInputRawBufferSize - 1);

#if INPUT_ENABLE_REPITCHED_BUFFER
	// If pitching up, use repitched buffer if possible
	if (stillWritingToRepitchedBuffer && repitchedBufferNumSamplesWritten && phaseIncrement > kMaxSampleValue) {
		int32_t howFarBackRepitched = ((uint64_t)howFarBack << 24) / (uint32_t)phaseIncrement + 1;
		if (repitchedBufferNumSamplesWritten >= howFarBackRepitched) {
			playHeads[PLAY_HEAD_NEWER].mode = PlayHeadMode::REPITCHED_BUFFER;
			playHeads[PLAY_HEAD_NEWER].repitchedBufferReadPos =
			    (uint32_t)(repitchedBufferWritePos - howFarBackRepitched) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
			goto thatsDone;
		}
	}
#endif
	// If still here, use raw direct buffer - usually because we're pitching down

	if (phase_increment == kMaxSampleValue) {
		play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_DIRECT;
		play_heads[PLAY_HEAD_NEWER].raw_buffer_read_pos =
		    num_raw_samples_processed_at_now_time & (kInputRawBufferSize - 1);
	}

	else {
		play_heads[PLAY_HEAD_NEWER].mode = PlayHeadMode::RAW_REPITCHING;

		// memset(playHeads[PLAY_HEAD_NEWER].interpolationBuffer, 0,
		// sizeof(playHeads[PLAY_HEAD_NEWER].interpolationBuffer));
		play_heads[PLAY_HEAD_NEWER].fill_interpolation_buffer(live_input_buffer, num_channels);
		play_heads[PLAY_HEAD_NEWER].osc_pos = additional_osc_pos;
	}

	play_heads[PLAY_HEAD_NEWER].perc_pos = 0xFFFFFFFF;

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

	consider_repitched_buffer(phase_increment);

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer && !stillWritingToRepitchedBuffer
	    && playHeads[PLAY_HEAD_NEWER].mode != PlayHeadMode::REPITCHED_BUFFER
	    && playHeads[PLAY_HEAD_OLDER].mode != PlayHeadMode::REPITCHED_BUFFER) {
		delugeDealloc(repitchedBuffer);
		repitchedBuffer = NULL;
	}
#endif
	samples_into_hop = 0;

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t timeTaken = endTime - startTime;
#endif
}

void LivePitchShifter::consider_repitched_buffer(int32_t /*phaseIncrement*/) {
#if INPUT_ENABLE_REPITCHED_BUFFER
	// Consider whether to have the repitchedBuffer
	if (phaseIncrement > kMaxSampleValue) {
		if (!repitchedBuffer) {

			repitchedBuffer =
			    (int32_t*)deluge::memory::alloc_fast(INPUT_REPITCHED_BUFFER_SIZE * sizeof(int32_t) * numChannels);
			if (repitchedBuffer) {
				repitchedBufferWritePos = 0;
				oscPos = 0;
				repitchedBufferNumSamplesWritten = 0;
				stillWritingToRepitchedBuffer = true;
				memset(interpolationBuffer, 0, sizeof(interpolationBuffer));
			}
		}
	}

	else {
		if (repitchedBuffer) {
			stillWritingToRepitchedBuffer = false;
		}
	}
#endif
}

bool LivePitchShifter::older_play_head_is_currently_sounding() {
	return (crossfade_progress < kMaxSampleValue);
}

bool LivePitchShifter::may_be_removed_without_click() {
	return (!older_play_head_is_currently_sounding() && play_heads[PLAY_HEAD_NEWER].mode == PlayHeadMode::RAW_DIRECT);
}

} // namespace deluge::synth
// NOLINTEND(bugprone-implicit-widening-of-multiplication-result,bugprone-narrowing-conversions,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-init-variables,cppcoreguidelines-narrowing-conversions,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-arrays,modernize-avoid-c-style-cast,readability-function-cognitive-complexity,readability-implicit-bool-conversion,readability-make-member-function-const,readability-redundant-casting,readability-use-std-min-max)
