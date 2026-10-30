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

			oscPos += phase_increment;
			int32_t numSamplesToJumpForward = oscPos >> 24;
			if (numSamplesToJumpForward) {
				oscPos &= 16777215;

				// If jumping forward by more than kInterpolationMaxNumSamples, we first need to jump to the one before
				// we're jumping forward to, to grab its value
				if (numSamplesToJumpForward > kInterpolationMaxNumSamples) {
					rawBufferReadPos = (rawBufferReadPos + (numSamplesToJumpForward - kInterpolationMaxNumSamples))
					                   & (kInputRawBufferSize - 1);

					// Shouldn't be necesssary, but for some reason this seems to do
					// some optimization and speed things up. Re-test?
					numSamplesToJumpForward = kInterpolationMaxNumSamples;
				}

				interpolator_.jump_forward(numSamplesToJumpForward);

				while (numSamplesToJumpForward--) {
					interpolator_.buffer_l[numSamplesToJumpForward] =
					    static_cast<int16_t>(raw_buffer[rawBufferReadPos * num_channels] >> 16);
					if (num_channels == 2) {
						interpolator_.buffer_r[numSamplesToJumpForward] =
						    static_cast<int16_t>(raw_buffer[rawBufferReadPos * 2 + 1] >> 16);
					}

					rawBufferReadPos = (rawBufferReadPos + 1) & (kInputRawBufferSize - 1);
				}
			}

			amplitude += amplitude_increment;

			deluge::dsp::StereoSample sample_read = (interpolation_buffer_size > 2)
			                                            ? interpolator_.interpolate(num_channels, which_kernel, oscPos)
			                                            : interpolator_.interpolate_linear(num_channels, oscPos);

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

			*output_buffer += multiply_32x32_rshift32_rounded(raw_buffer[rawBufferReadPos * num_channels], amplitude)
			                  << 4;
			output_buffer++;

			if (num_channels == 2) {
				*output_buffer += multiply_32x32_rshift32_rounded(raw_buffer[rawBufferReadPos * 2 + 1], amplitude) << 4;
				output_buffer++;
			}
			rawBufferReadPos = (rawBufferReadPos + 1) & (kInputRawBufferSize - 1);
		} while (output_buffer != output_buffer_end);
	}
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio Only valid if phaseIncrement > kMaxSampleValue
// NOLINTNEXTLINE(readability-make-member-function-const): the header changes with the pitch-shifter rewrite (TODO.md)
int32_t LivePitchShifterPlayHead::getEstimatedPlaytimeRemaining([[maybe_unused]] uint32_t repitched_buffer_write_pos,
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
		uint32_t howFarBackRaw =
		    (live_input_buffer->numRawSamplesProcessed - rawBufferReadPos) & (kInputRawBufferSize - 1);
		how_far_back = (static_cast<uint64_t>(howFarBackRaw) << 24) / static_cast<uint32_t>(phase_increment);
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
int32_t LivePitchShifterPlayHead::getNumRawSamplesBehindInput(LiveInputBuffer* live_input_buffer,
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
		return static_cast<int32_t>((live_input_buffer->numRawSamplesProcessed - rawBufferReadPos)
		                            & (kInputRawBufferSize - 1));
	}

	else { // DIRECT
		return 0;
	}
}

/// @param numChannels a value between 0 and 1
void LivePitchShifterPlayHead::fillInterpolationBuffer(LiveInputBuffer* liveInputBuffer, int32_t numChannels) {
#pragma unroll
	for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
		int32_t pos = (uint32_t)(rawBufferReadPos - i) & (kInputRawBufferSize - 1);

		interpolator_.buffer_l[i - 1] = static_cast<int16_t>(std::cmp_less(pos, liveInputBuffer->numRawSamplesProcessed)
		                                                         ? liveInputBuffer->rawBuffer[pos * numChannels] >> 16
		                                                         : 0);
	}

	if (numChannels > 1) {
#pragma unroll
		for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
			int32_t pos = (uint32_t)(rawBufferReadPos - i) & (kInputRawBufferSize - 1);

			interpolator_.buffer_r[i - 1] =
			    static_cast<int16_t>(std::cmp_less(pos, liveInputBuffer->numRawSamplesProcessed)
			                             ? liveInputBuffer->rawBuffer[pos * numChannels + 1] >> 16
			                             : 0);
		}
	}
}

} // namespace deluge::synth
