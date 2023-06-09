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

#ifndef LIVEPITCHSHIFTER_H_
#define LIVEPITCHSHIFTER_H_

#include "definitions.h"
#include "LivePitchShifterPlayHead.h"

class LiveInputBuffer;

class LivePitchShifter {
public:
	LivePitchShifter(int newInputType, int32_t phaseIncrement);
	~LivePitchShifter();
	void giveInput(int numSamples, int inputType, int32_t phaseIncrement);
	void render(int32_t* outputBuffer, int numSamplesThisFunctionCall, int32_t phaseIncrement, int32_t amplitude,
	            int32_t amplitudeIncrement, int interpolationBufferSize);

	bool mayBeRemovedWithoutClick();

#if INPUT_ENABLE_REPITCHED_BUFFER
	void interpolate(int32_t* sampleRead, int interpolationBufferSize, int numChannelsNow, int whichKernel);
	int32_t* repitchedBuffer;
	int repitchedBufferWritePos;
	uint64_t repitchedBufferNumSamplesWritten;
	bool stillWritingToRepitchedBuffer;
	int32_t interpolationBuffer[2][INTERPOLATION_MAX_NUM_SAMPLES];
	uint32_t oscPos;
#endif

	int8_t numChannels;
	uint8_t inputType;

	uint32_t crossfadeProgress; // Out of 16777216
	uint32_t crossfadeIncrement;
	int32_t nextCrossfadeLength;
	int32_t samplesTilHopEnd;
	int32_t samplesIntoHop;

	int percThresholdForCut;

	LivePitchShifterPlayHead playHeads[2];

private:
	void hopEnd(int32_t phaseIncrement, LiveInputBuffer* liveInputBuffer, uint64_t numRawSamplesProcessed,
	            uint64_t numRawSamplesProcessedLatest);
	void considerRepitchedBuffer(int32_t phaseIncrement);
	bool olderPlayHeadIsCurrentlySounding();
};

#endif /* LIVEPITCHSHIFTER_H_ */
