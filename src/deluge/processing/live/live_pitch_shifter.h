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
#include "processing/live/live_pitch_shifter_play_head.h"

class LiveInputBuffer;

class LivePitchShifter {
public:
	LivePitchShifter(OscType newInputType, int32_t phaseIncrement);
	~LivePitchShifter();
	void giveInput(int32_t numSamples, int32_t inputType, int32_t phaseIncrement);
	void render(int32_t* outputBuffer, int32_t numSamplesThisFunctionCall, int32_t phaseIncrement, int32_t amplitude,
	            int32_t amplitudeIncrement, int32_t interpolationBufferSize);

	bool mayBeRemovedWithoutClick();

#if INPUT_ENABLE_REPITCHED_BUFFER
	void interpolate(int32_t* sampleRead, int32_t interpolationBufferSize, int32_t numChannelsNow, int32_t whichKernel);
	int32_t* repitchedBuffer;
	int32_t repitchedBufferWritePos;
	uint64_t repitchedBufferNumSamplesWritten;
	bool stillWritingToRepitchedBuffer;
	int32_t interpolationBuffer[2][kInterpolationMaxNumSamples];
	uint32_t oscPos;
#endif

	int8_t numChannels;
	OscType inputType;

	uint32_t crossfadeProgress; // Out of kMaxSampleValue
	// crossfadeIncrement + percThresholdForCut are only set at the first hop (before the crossfade/perc logic that
	// reads them becomes reachable), so default-initialise them — otherwise a freshly-allocated shifter carries
	// read-before-write garbage in these fields.
	uint32_t crossfadeIncrement{0};
	int32_t nextCrossfadeLength;
	int32_t samplesTilHopEnd;
	int32_t samplesIntoHop;

	int32_t percThresholdForCut{0};

	LivePitchShifterPlayHead playHeads[2];

private:
	void hopEnd(int32_t phaseIncrement, LiveInputBuffer* liveInputBuffer, uint64_t numRawSamplesProcessed,
	            uint64_t numRawSamplesProcessedLatest);
	void considerRepitchedBuffer(int32_t phaseIncrement);
	bool olderPlayHeadIsCurrentlySounding();
};
