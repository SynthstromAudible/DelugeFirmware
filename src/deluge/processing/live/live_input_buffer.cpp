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

#include "processing/live/live_input_buffer.h"
#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
#include <string.h>

extern "C" {
#include "drivers/ssi/ssi.h"
}

LiveInputBuffer::LiveInputBuffer() {
	upToTime = 0;
	numRawSamplesProcessed = 0;
}

void LiveInputBuffer::giveInput(int32_t numSamples, uint32_t currentTime, OscType inputType) {

	if (upToTime == (uint32_t)(currentTime + numSamples)) {
		return; // It's already been done
	}

	// Or if we need to reset everything cos we missed some
	if (upToTime != currentTime) {
		numRawSamplesProcessed = 0;
		lastSampleRead = 0;
		lastAngle = 0;
		memset(angleLPFMem, 0, sizeof(angleLPFMem));
	}

	int32_t const* __restrict__ inputReadPos = (int32_t const*)AudioEngine::i2sRXBufferPos;

	uint32_t endNumRawSamplesProcessed = numRawSamplesProcessed + numSamples;

	do {

		int32_t thisSampleRead;

		if (inputType == OscType::INPUT_L) {
			thisSampleRead = inputReadPos[0] >> 2;
			rawBuffer[numRawSamplesProcessed & (kInputRawBufferSize - 1)] = inputReadPos[0];
		}
		else if (inputType == OscType::INPUT_R) {
			thisSampleRead = inputReadPos[1] >> 2;
			rawBuffer[numRawSamplesProcessed & (kInputRawBufferSize - 1)] = inputReadPos[1];
		}
		else { // STEREO
			thisSampleRead = (inputReadPos[0] >> 2) + (inputReadPos[1] >> 2);
			rawBuffer[(numRawSamplesProcessed & (kInputRawBufferSize - 1)) * 2] = inputReadPos[0];
			rawBuffer[(numRawSamplesProcessed & (kInputRawBufferSize - 1)) * 2 + 1] = inputReadPos[1];
		}

		int32_t angle = thisSampleRead - lastSampleRead;
		lastSampleRead = thisSampleRead;
		if (angle < 0) {
			angle = -angle;
		}

		for (int32_t p = 0; p < kDifferenceLPFPoles; p++) {
			int32_t distanceToGo = angle - angleLPFMem[p];
			angleLPFMem[p] += multiply_32x32_rshift32_rounded(distanceToGo, 1 << 23); // distanceToGo >> 9;

			angle = angleLPFMem[p];
		}

		if ((numRawSamplesProcessed & (kPercBufferReductionSize - 1)) == 0) {

			int32_t difference = angle - lastAngle;
			if (difference < 0) {
				difference = -difference;
			}

			int32_t percussiveness = ((uint64_t)difference * 262144 / angle) >> 1;

			percussiveness = getTanH<23>(percussiveness);

			percBuffer[(numRawSamplesProcessed >> kPercBufferReductionMagnitude) & (kInputPercBufferSize - 1)] =
			    percussiveness;
		}
		lastAngle = angle;

		inputReadPos += NUM_MONO_INPUT_CHANNELS;
		if (inputReadPos >= getRxBufferEnd()) {
			inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
		}

		numRawSamplesProcessed++;
	} while (numRawSamplesProcessed != endNumRawSamplesProcessed);

	upToTime = currentTime + numSamples;
}

bool LiveInputBuffer::getAveragesForCrossfade(int32_t* totals, int32_t startPos, int32_t lengthToAverageEach,
                                              int32_t numChannels) {

	int32_t currentPos = startPos;
	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {

		totals[i] = 0;
		int32_t endPos = (currentPos + lengthToAverageEach) & (kInputRawBufferSize - 1);

		// Splitting this in two by numChannels makes it a bit faster for mono, but slower for stereo
		do {
			totals[i] += rawBuffer[currentPos * numChannels] >> 16;
			if (numChannels == 2) {
				totals[i] += rawBuffer[currentPos * 2 + 1] >> 16;
			}

			currentPos = (currentPos + 1) & (kInputRawBufferSize - 1);
		} while (currentPos != endPos);
	}

	return true;
}
