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

#pragma once

#include "arm_neon_shim.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/interpolation/interpolator.h"
#include <array>

namespace deluge::synth {

class LivePitchShifter;
class LiveInputBuffer;

enum class PlayHeadMode {
	REPITCHED_BUFFER,
	RAW_REPITCHING,
	RAW_DIRECT,
};

struct LivePitchShifterPlayHead {
	void render(int32_t* output_buffer, int32_t num_samples, int32_t num_channels, int32_t phase_increment,
	            int32_t amplitude, int32_t amplitude_increment, int32_t* repitched_buffer, int32_t* raw_buffer,
	            int32_t which_kernel, int32_t interpolation_buffer_size);
	int32_t get_estimated_playtime_remaining(uint32_t repitched_buffer_write_pos, LiveInputBuffer* live_input_buffer,
	                                         int32_t phase_increment);

	int32_t get_num_raw_samples_behind_input(LiveInputBuffer* live_input_buffer, LivePitchShifter* live_pitch_shifter,
	                                         int32_t phase_increment);

	void fill_interpolation_buffer(LiveInputBuffer* live_input_buffer, int32_t num_channels);

	// Default member initializers: this class has no constructor and the OLDER play
	// head is never explicitly initialized by LivePitchShifter, so on hardware its
	// fields would be stale heap bytes (the sim/golden builds zero them, masking it —
	// render() reads `mode` / play positions, so garbage here corrupts the pitch
	// shift). Zero matches the sim behaviour (`mode == REPITCHED_BUFFER`).
	PlayHeadMode mode = PlayHeadMode::REPITCHED_BUFFER;
#if INPUT_ENABLE_REPITCHED_BUFFER
	int32_t repitchedBufferReadPos = 0;
#endif
	int32_t raw_buffer_read_pos = 0;
	uint32_t osc_pos = 0;

	deluge::dsp::fixed::Interpolator interpolator_;

	uint32_t perc_pos = 0;
};

} // namespace deluge::synth
