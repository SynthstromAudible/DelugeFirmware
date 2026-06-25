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
#include "dsp/interpolate/interpolate.h"
#include <array>

class LivePitchShifter;
class LiveInputBuffer;

enum class PlayHeadMode {
	REPITCHED_BUFFER,
	RAW_REPITCHING,
	RAW_DIRECT,
};

struct LivePitchShifterPlayHead {
	void render(int32_t* outputBuffer, int32_t numSamples, int32_t numChannels, int32_t phaseIncrement,
	            int32_t amplitude, int32_t amplitudeIncrement, int32_t* repitchedBuffer, int32_t* rawBuffer,
	            int32_t whichKernel, int32_t interpolationBufferSize);
	int32_t getEstimatedPlaytimeRemaining(uint32_t repitchedBufferWritePos, LiveInputBuffer* liveInputBuffer,
	                                      int32_t phaseIncrement);

	int32_t getNumRawSamplesBehindInput(LiveInputBuffer* liveInputBuffer, LivePitchShifter* livePitchShifter,
	                                    int32_t phaseIncrement);

	void fillInterpolationBuffer(LiveInputBuffer* liveInputBuffer, int32_t numChannels);

	// Default-initialised so a freshly-constructed play-head is fully defined: the OLDER head in particular is never
	// touched by LivePitchShifter's constructor (it's only populated at the first hop, before it becomes audible),
	// so without these its fields would be read-before-write garbage if ever inspected early.
	PlayHeadMode mode{PlayHeadMode::RAW_DIRECT};
#if INPUT_ENABLE_REPITCHED_BUFFER
	int32_t repitchedBufferReadPos{0};
#endif
	int32_t rawBufferReadPos{0};
	uint32_t oscPos{0};

	deluge::dsp::Interpolator interpolator_;

	uint32_t percPos{0};
};
