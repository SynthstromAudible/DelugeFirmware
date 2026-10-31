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

#include "synth/sources/input/live/live_pitch_shifter_play_head.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/fixed/interpolation/interpolator.h"
#include "synth/sources/input/live/live_input_buffer.h"
#include "synth/sources/input/live/live_pitch_shifter.h"
#include <argon.hpp>
#include <utility> // std::cmp_less

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#ifdef __ARM_NEON
#pragma GCC target("fpu=neon") // no-op (and invalid) off-target; SIMDe supplies NEON on the host
#endif
#endif

#include "arm_neon_shim.h"

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion,readability-make-member-function-const)

namespace deluge::synth {

// NOLINTNEXTLINE(readability-function-cognitive-complexity): split by the pitch-shifter rewrite (TODO.md)
void LivePitchShifterPlayHead::render(int32_t* __restrict__ output_buffer, int32_t num_samples, int32_t num_channels,
                                      int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
                                      [[maybe_unused]] int32_t* repitched_buffer, int32_t* raw_buffer,
                                      int32_t which_kernel, int32_t interpolation_buffer_size) {

	int32_t* output_buffer_end = output_buffer + num_samples * num_channels;

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		do {
			amplitude += amplitude_increment;

			*output_buffer +=
			    multiply_32x32_rshift32_rounded(repitched_buffer[repitchedBufferReadPos * num_channels], amplitude)
			    << 5;
			output_buffer++;

			if (num_channels == 2) {
				*outputBufferR +=
				    multiply_32x32_rshift32_rounded(repitched_buffer[repitchedBufferReadPos * 2 + 1], amplitude) << 5;
				outputBufferR++;
			}

			repitchedBufferReadPos = (repitchedBufferReadPos + 1) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		} while (output_buffer != output_buffer_end);
	}

	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		do {

			osc_pos += phase_increment;
			int32_t num_samples_to_jump_forward = osc_pos >> 24;
			if (num_samples_to_jump_forward) {
				osc_pos &= 16777215;

				// If jumping forward by more than kInterpolationMaxNumSamples, we first need to jump to the one before
				// we're jumping forward to, to grab its value
				if (num_samples_to_jump_forward > kInterpolationMaxNumSamples) {
					raw_buffer_read_pos =
					    (raw_buffer_read_pos + (num_samples_to_jump_forward - kInterpolationMaxNumSamples))
					    & (kInputRawBufferSize - 1);

					// Shouldn't be necesssary, but for some reason this seems to do
					// some optimization and speed things up. Re-test?
					num_samples_to_jump_forward = kInterpolationMaxNumSamples;
				}

				interpolator_.jump_forward(num_samples_to_jump_forward);

				while (num_samples_to_jump_forward--) {
					interpolator_.buffer_l[num_samples_to_jump_forward] =
					    static_cast<int16_t>(raw_buffer[raw_buffer_read_pos * num_channels] >> 16);
					if (num_channels == 2) {
						interpolator_.buffer_r[num_samples_to_jump_forward] =
						    static_cast<int16_t>(raw_buffer[raw_buffer_read_pos * 2 + 1] >> 16);
					}

					raw_buffer_read_pos = (raw_buffer_read_pos + 1) & (kInputRawBufferSize - 1);
				}
			}

			amplitude += amplitude_increment;

			deluge::dsp::StereoSample sample_read = (interpolation_buffer_size > 2)
			                                            ? interpolator_.interpolate(num_channels, which_kernel, osc_pos)
			                                            : interpolator_.interpolate_linear(num_channels, osc_pos);

			*output_buffer += multiply_32x32_rshift32_rounded(sample_read.l, amplitude) << 5;
			output_buffer++;

			if (num_channels == 2) {
				*output_buffer += multiply_32x32_rshift32_rounded(sample_read.r, amplitude) << 5;
				output_buffer++;
			}
		} while (output_buffer != output_buffer_end);
	}

	else { // DIRECT
		do {
			amplitude += amplitude_increment;

			*output_buffer += multiply_32x32_rshift32_rounded(raw_buffer[raw_buffer_read_pos * num_channels], amplitude)
			                  << 4;
			output_buffer++;

			if (num_channels == 2) {
				*output_buffer += multiply_32x32_rshift32_rounded(raw_buffer[raw_buffer_read_pos * 2 + 1], amplitude)
				                  << 4;
				output_buffer++;
			}
			raw_buffer_read_pos = (raw_buffer_read_pos + 1) & (kInputRawBufferSize - 1);
		} while (output_buffer != output_buffer_end);
	}
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio Only valid if phaseIncrement > kMaxSampleValue
// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
int32_t LivePitchShifterPlayHead::get_estimated_playtime_remaining([[maybe_unused]] uint32_t repitched_buffer_write_pos,
                                                                   LiveInputBuffer* live_input_buffer,
                                                                   int32_t phase_increment) {
	uint32_t how_far_back = 0;
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		how_far_back =
		    (uint32_t)(repitched_buffer_write_pos - repitchedBufferReadPos) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		uint32_t how_far_back_raw =
		    (live_input_buffer->num_raw_samples_processed - raw_buffer_read_pos) & (kInputRawBufferSize - 1);
		how_far_back = (static_cast<uint64_t>(how_far_back_raw) << 24) / static_cast<uint32_t>(phase_increment);
	}
	else {                 // DIRECT
		return 2147483647; // It'd never run out
	}

	uint64_t estimate =
	    (static_cast<uint64_t>(how_far_back) << 24) / static_cast<uint32_t>(phase_increment - kMaxSampleValue);
	if (estimate >= 2147483647) {
		return 2147483647;
	}
	else {
		return estimate;
	}
}

// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
int32_t
LivePitchShifterPlayHead::get_num_raw_samples_behind_input(LiveInputBuffer* live_input_buffer,
                                                           [[maybe_unused]] LivePitchShifter* live_pitch_shifter,
                                                           [[maybe_unused]] int32_t phase_increment) {
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		uint32_t howFarBackRepitched = (uint32_t)(live_pitch_shifter->repitchedBufferWritePos - repitchedBufferReadPos)
		                               & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		return ((uint64_t)howFarBackRepitched * phase_increment) >> 24;
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		return static_cast<int32_t>((live_input_buffer->num_raw_samples_processed - raw_buffer_read_pos)
		                            & (kInputRawBufferSize - 1));
	}

	else { // DIRECT
		return 0;
	}
}

/// @param numChannels a value between 0 and 1
void LivePitchShifterPlayHead::fill_interpolation_buffer(LiveInputBuffer* live_input_buffer, int32_t num_channels) {
#pragma unroll
	for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
		int32_t pos = (uint32_t)(raw_buffer_read_pos - i) & (kInputRawBufferSize - 1);

		interpolator_.buffer_l[i - 1] =
		    static_cast<int16_t>(std::cmp_less(pos, live_input_buffer->num_raw_samples_processed)
		                             ? live_input_buffer->raw_buffer[pos * num_channels] >> 16
		                             : 0);
	}

	if (num_channels > 1) {
#pragma unroll
		for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
			int32_t pos = (uint32_t)(raw_buffer_read_pos - i) & (kInputRawBufferSize - 1);

			interpolator_.buffer_r[i - 1] =
			    static_cast<int16_t>(std::cmp_less(pos, live_input_buffer->num_raw_samples_processed)
			                             ? live_input_buffer->raw_buffer[pos * num_channels + 1] >> 16
			                             : 0);
		}
	}
}

} // namespace deluge::synth
// NOLINTEND(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,modernize-avoid-c-style-cast,readability-implicit-bool-conversion,readability-make-member-function-const)
