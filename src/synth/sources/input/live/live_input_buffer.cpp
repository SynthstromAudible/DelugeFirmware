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

#include "synth/sources/input/live/live_input_buffer.h"
#include "board_config.h" // NUM_MONO_INPUT_CHANNELS, SSI_RX_BUFFER_NUM_SAMPLES
#include "definitions_cxx.hpp"
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32_rounded
#include "dsp-next/fixed/shaping/tanh.h"
#include "synth/sources/input/audio_input_port.h"
#include <string.h>

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(cppcoreguidelines-init-variables,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-style-cast,readability-redundant-casting)

namespace deluge::synth {

LiveInputBuffer::LiveInputBuffer() {
	up_to_time = 0;
	num_raw_samples_processed = 0;
	last_sample_read = 0;
	last_angle = 0;
	memset(angle_lpf_mem, 0, sizeof(angle_lpf_mem));
}

void LiveInputBuffer::give_input(int32_t num_samples, AudioInputPort& port, InputChannel channel) {

	uint32_t current_time = port.sample_time();

	if (up_to_time == (uint32_t)(current_time + num_samples)) {
		return; // It's already been done
	}

	// Or if we need to reset everything cos we missed some
	if (up_to_time != current_time) {
		num_raw_samples_processed = 0;
		last_sample_read = 0;
		last_angle = 0;
		memset(angle_lpf_mem, 0, sizeof(angle_lpf_mem));
	}

	int32_t const* __restrict__ input_read_pos = port.ring_read_pos();
	int32_t const* const input_ring_end = port.ring_end();

	uint32_t end_num_raw_samples_processed = num_raw_samples_processed + num_samples;

	do {

		int32_t this_sample_read;

		if (channel == InputChannel::LEFT) {
			this_sample_read = input_read_pos[0] >> 2;
			raw_buffer[num_raw_samples_processed & (kInputRawBufferSize - 1)] = input_read_pos[0];
		}
		else if (channel == InputChannel::RIGHT) {
			this_sample_read = input_read_pos[1] >> 2;
			raw_buffer[num_raw_samples_processed & (kInputRawBufferSize - 1)] = input_read_pos[1];
		}
		else { // STEREO
			this_sample_read = (input_read_pos[0] >> 2) + (input_read_pos[1] >> 2);
			raw_buffer[(num_raw_samples_processed & (kInputRawBufferSize - 1)) * 2] = input_read_pos[0];
			raw_buffer[(num_raw_samples_processed & (kInputRawBufferSize - 1)) * 2 + 1] = input_read_pos[1];
		}

		int32_t angle = this_sample_read - last_sample_read;
		last_sample_read = this_sample_read;
		if (angle < 0) {
			angle = -angle;
		}

		for (int32_t p = 0; p < kDifferenceLPFPoles; p++) {
			int32_t distance_to_go = angle - angle_lpf_mem[p];
			angle_lpf_mem[p] += multiply_32x32_rshift32_rounded(distance_to_go, 1 << 23); // distanceToGo >> 9;

			angle = angle_lpf_mem[p];
		}

		if ((num_raw_samples_processed & (kPercBufferReductionSize - 1)) == 0) {

			int32_t difference = angle - last_angle;
			if (difference < 0) {
				difference = -difference;
			}

			// guard against dividing by 0
			int32_t percussiveness = angle ? (((uint64_t)difference * 262144 / angle) >> 1) : 0;

			percussiveness = dsp::fixed::tanh<23>(percussiveness);

			perc_buffer[(num_raw_samples_processed >> kPercBufferReductionMagnitude) & (kInputPercBufferSize - 1)] =
			    percussiveness;
		}
		last_angle = angle;

		input_read_pos += NUM_MONO_INPUT_CHANNELS;
		if (input_read_pos >= input_ring_end) {
			input_read_pos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
		}

		num_raw_samples_processed++;
	} while (num_raw_samples_processed != end_num_raw_samples_processed);

	up_to_time = current_time + num_samples;
}

bool LiveInputBuffer::get_averages_for_crossfade(int32_t* totals, int32_t start_pos, int32_t length_to_average_each,
                                                 int32_t num_channels) {

	int32_t current_pos = start_pos;
	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {

		totals[i] = 0;
		int32_t end_pos = (current_pos + length_to_average_each) & (kInputRawBufferSize - 1);

		// Splitting this in two by numChannels makes it a bit faster for mono, but slower for stereo
		do {
			totals[i] += raw_buffer[current_pos * num_channels] >> 16;
			if (num_channels == 2) {
				totals[i] += raw_buffer[current_pos * 2 + 1] >> 16;
			}

			current_pos = (current_pos + 1) & (kInputRawBufferSize - 1);
		} while (current_pos != end_pos);
	}

	return true;
}

} // namespace deluge::synth
// NOLINTEND(cppcoreguidelines-init-variables,cppcoreguidelines-prefer-member-initializer,modernize-avoid-c-style-cast,readability-redundant-casting)
