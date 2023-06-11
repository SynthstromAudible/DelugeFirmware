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

#ifndef LIVEPITCHSHIFTERPLAYHEAD_H_
#define LIVEPITCHSHIFTERPLAYHEAD_H_

#include "definitions.h"

typedef __simd64_int16_t int16x4_t;

#define PLAY_HEAD_MODE_REPITCHED_BUFFER 0
#define PLAY_HEAD_MODE_RAW_REPITCHING 1
#define PLAY_HEAD_MODE_RAW_DIRECT 2
class LivePitchShifter;
class LiveInputBuffer;

class LivePitchShifterPlayHead {
public:
	LivePitchShifterPlayHead();
	~LivePitchShifterPlayHead();
	void render(int32_t* outputBuffer, int numSamples, int numChannels, int32_t phaseIncrement, int32_t amplitude,
	            int32_t amplitudeIncrement, int32_t* repitchedBuffer, int32_t* rawBuffer, int whichKernel,
	            int interpolationBufferSize);
	int getEstimatedPlaytimeRemaining(uint32_t repitchedBufferWritePos, LiveInputBuffer* liveInputBuffer,
	                                  int32_t phaseIncrement);

	int getNumRawSamplesBehindInput(LiveInputBuffer* liveInputBuffer, LivePitchShifter* livePitchShifter,
	                                int32_t phaseIncrement);

	void fillInterpolationBuffer(LiveInputBuffer* liveInputBuffer, int numChannels);

	uint8_t mode;
#if INPUT_ENABLE_REPITCHED_BUFFER
	int repitchedBufferReadPos;
#endif
	int rawBufferReadPos;
	uint32_t oscPos;

	int16x4_t interpolationBuffer[2][INTERPOLATION_MAX_NUM_SAMPLES >> 2];

	uint32_t percPos;

private:
	void interpolate(int32_t* sampleRead, int numChannelsNow, int whichKernel);
	void interpolateLinear(int32_t* sampleRead, int numChannelsNow, int whichKernel);
};

#endif /* LIVEPITCHSHIFTERPLAYHEAD_H_ */
