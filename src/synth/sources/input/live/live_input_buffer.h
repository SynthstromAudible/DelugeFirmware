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

#include "definitions_cxx.hpp"
#include "synth/sources/input/input_channel.h"
#include <cstdint>

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

namespace deluge::synth {

class AudioInputPort;

class LiveInputBuffer {
public:
	LiveInputBuffer();
	~LiveInputBuffer() = default;
	/// Ingest @p numSamples of fresh input (via @p port — the ring + frame clock) and run the angle/perc analysis.
	void give_input(int32_t num_samples, AudioInputPort& port, InputChannel channel);
	bool get_averages_for_crossfade(int32_t* totals, int32_t start_pos, int32_t length_to_average_each,
	                                int32_t num_channels);

	uint32_t up_to_time;

	uint32_t num_raw_samples_processed;

	int32_t last_sample_read{};
	int32_t last_angle{};
	int32_t angle_lpf_mem[kDifferenceLPFPoles]{};

	// Zero-initialised: the hop-cut decision reads percBuffer positions ahead of what giveInput has written during
	// warm-up, so an unwritten slot must be a defined 0 rather than recycled allocator content.
	uint8_t perc_buffer[kInputPercBufferSize]{};

	// Must be last!!! Cos we're gonna allocate and access it double-length for stereo
	int32_t raw_buffer[kInputRawBufferSize]{};
};

} // namespace deluge::synth
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
