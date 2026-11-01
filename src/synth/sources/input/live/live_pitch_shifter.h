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
#include "dsp-next/fixed/noise/cong.h"
#include "synth/sources/input/input_channel.h"
#include "synth/sources/input/live/live_pitch_shifter_play_head.h"

// The live pitch shifter keeps its legacy arithmetic as it is until its rewrite (TODO.md).
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

namespace deluge::synth {

class LiveInputBuffer;
class AudioInputPort;

class LivePitchShifter {
public:
	LivePitchShifter(InputChannel channel, int32_t phase_increment);
	~LivePitchShifter();

	/// Render @p numSamplesThisFunctionCall of pitch-shifted output, summed into @p outputBuffer.
	/// @return the number of time-stretch hops performed this call. The caller folds this into the host's
	/// per-routine hop accounting (CPU-load spreading) — the pitch-shifter itself stays free of that global.
	[[nodiscard]] int32_t render(int32_t* output_buffer, int32_t num_samples_this_function_call,
	                             int32_t phase_increment, int32_t amplitude, int32_t amplitude_increment,
	                             int32_t interpolation_buffer_size, AudioInputPort& port, dsp::fixed::Cong& noise);

	bool may_be_removed_without_click();

#if INPUT_ENABLE_REPITCHED_BUFFER
	int32_t* repitchedBuffer;
	int32_t repitchedBufferWritePos;
	uint64_t repitchedBufferNumSamplesWritten;
	bool stillWritingToRepitchedBuffer;
	int32_t interpolationBuffer[2][kInterpolationMaxNumSamples];
	uint32_t oscPos;
#endif

	int8_t num_channels;
	InputChannel channel;

	uint32_t crossfade_progress; // Out of kMaxSampleValue
	// crossfadeIncrement + percThresholdForCut are only set at the first hop (before the crossfade/perc logic that
	// reads them becomes reachable), so default-initialise them — otherwise a freshly-allocated shifter carries
	// read-before-write garbage in these fields.
	uint32_t crossfade_increment{0};
	int32_t next_crossfade_length;
	int32_t samples_til_hop_end;
	int32_t samples_into_hop;

	int32_t perc_threshold_for_cut{0};

	LivePitchShifterPlayHead play_heads[2];

private:
	void hop_end(int32_t phase_increment, LiveInputBuffer* live_input_buffer, uint64_t num_raw_samples_processed,
	             uint64_t num_raw_samples_processed_latest, dsp::fixed::Cong& noise);
	void consider_repitched_buffer(int32_t phase_increment);
	bool older_play_head_is_currently_sounding();
};

} // namespace deluge::synth
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
