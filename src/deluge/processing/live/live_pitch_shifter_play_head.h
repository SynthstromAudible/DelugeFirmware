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
#include <array>

class LivePitchShifter;
class LiveInputBuffer;

enum class PlayHeadMode {
	REPITCHED_BUFFER,
	RAW_REPITCHING,
	RAW_DIRECT,
};

class LivePitchShifterPlayHead {
public:
	LivePitchShifterPlayHead();
	~LivePitchShifterPlayHead();
	void render(int32_t* outputBuffer, int32_t numSamples, int32_t numChannels, int32_t phaseIncrement,
	            int32_t amplitude, int32_t amplitudeIncrement, int32_t* repitchedBuffer, int32_t* rawBuffer,
	            int32_t whichKernel, int32_t interpolationBufferSize);
	int32_t getEstimatedPlaytimeRemaining(uint32_t repitchedBufferWritePos, LiveInputBuffer* liveInputBuffer,
	                                      int32_t phaseIncrement);

	int32_t getNumRawSamplesBehindInput(LiveInputBuffer* liveInputBuffer, LivePitchShifter* livePitchShifter,
	                                    int32_t phaseIncrement);

	void fillInterpolationBuffer(LiveInputBuffer* liveInputBuffer, int32_t numChannels);

	PlayHeadMode mode;
#if INPUT_ENABLE_REPITCHED_BUFFER
	int32_t repitchedBufferReadPos;
#endif
	int32_t rawBufferReadPos;
	uint32_t oscPos;

	std::array<std::array<int16x4_t, kInterpolationMaxNumSamples / 4>, 2> interpolationBuffer;

	uint32_t percPos;
};
