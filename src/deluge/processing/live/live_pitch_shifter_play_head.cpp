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

#include "processing/live/live_pitch_shifter_play_head.h"
#include "dsp/interpolate/interpolate.h"
#include "processing/live/live_input_buffer.h"
#include "processing/live/live_pitch_shifter.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <argon.hpp>

#pragma GCC push_options
#pragma GCC target("fpu=neon")

#include "arm_neon_shim.h"

LivePitchShifterPlayHead::LivePitchShifterPlayHead() {
	// TODO Auto-generated constructor stub
}

LivePitchShifterPlayHead::~LivePitchShifterPlayHead() {
	// TODO Auto-generated destructor stub
}

void LivePitchShifterPlayHead::render(int32_t* __restrict__ outputBuffer, int32_t numSamples, int32_t numChannels,
                                      int32_t phaseIncrement, int32_t amplitude, int32_t amplitudeIncrement,
                                      int32_t* repitchedBuffer, int32_t* rawBuffer, int32_t whichKernel,
                                      int32_t interpolationBufferSize) {

	int32_t* outputBufferEnd = outputBuffer + numSamples * numChannels;

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		do {
			amplitude += amplitudeIncrement;

			*outputBuffer +=
			    multiply_32x32_rshift32_rounded(repitchedBuffer[repitchedBufferReadPos * numChannels], amplitude) << 5;
			outputBuffer++;

			if (numChannels == 2) {
				*outputBufferR +=
				    multiply_32x32_rshift32_rounded(repitchedBuffer[repitchedBufferReadPos * 2 + 1], amplitude) << 5;
				outputBufferR++;
			}

			repitchedBufferReadPos = (repitchedBufferReadPos + 1) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		} while (outputBuffer != outputBufferEnd);
	}

	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		do {

			oscPos += phaseIncrement;
			int32_t numSamplesToJumpForward = oscPos >> 24;
			if (numSamplesToJumpForward) {
				oscPos &= 16777215;

				// If jumping forward by more than kInterpolationMaxNumSamples, we first need to jump to the one before
				// we're jumping forward to, to grab its value
				if (numSamplesToJumpForward > kInterpolationMaxNumSamples) {
					rawBufferReadPos = (rawBufferReadPos + (numSamplesToJumpForward - kInterpolationMaxNumSamples))
					                   & (kInputRawBufferSize - 1);
					numSamplesToJumpForward =
					    kInterpolationMaxNumSamples; // Shouldn't be necesssary, but for some reason this seems to do
					                                 // some optimization and speed things up. Re-test?
				}

				for (int32_t i = kInterpolationMaxNumSamples - 1; i >= numSamplesToJumpForward; i--) {
					interpolationBuffer[0][0][i] = interpolationBuffer[0][0][i - numSamplesToJumpForward];
				}

				if (numChannels == 2) {
					for (int32_t i = kInterpolationMaxNumSamples - 1; i >= numSamplesToJumpForward; i--) {
						interpolationBuffer[1][0][i] = interpolationBuffer[1][0][i - numSamplesToJumpForward];
					}
				}

				while (numSamplesToJumpForward--) {
					interpolationBuffer[0][0][numSamplesToJumpForward] =
					    rawBuffer[rawBufferReadPos * numChannels] >> 16;
					if (numChannels == 2) {
						interpolationBuffer[1][0][numSamplesToJumpForward] = rawBuffer[rawBufferReadPos * 2 + 1] >> 16;
					}

					rawBufferReadPos = (rawBufferReadPos + 1) & (kInputRawBufferSize - 1);
				}
			}

			amplitude += amplitudeIncrement;

			int32_t sampleRead[2];
			if (interpolationBufferSize > 2) {
				deluge::dsp::interpolate(sampleRead, numChannels, whichKernel, oscPos, interpolationBuffer);
			}
			else {
				deluge::dsp::interpolateLinear(sampleRead, numChannels, whichKernel, oscPos, interpolationBuffer);
			}

			*outputBuffer += multiply_32x32_rshift32_rounded(sampleRead[0], amplitude) << 5;
			outputBuffer++;

			if (numChannels == 2) {
				*outputBuffer += multiply_32x32_rshift32_rounded(sampleRead[1], amplitude) << 5;
				outputBuffer++;
			}
		} while (outputBuffer != outputBufferEnd);
	}

	else { // DIRECT
		do {
			amplitude += amplitudeIncrement;

			*outputBuffer += multiply_32x32_rshift32_rounded(rawBuffer[rawBufferReadPos * numChannels], amplitude) << 4;
			outputBuffer++;

			if (numChannels == 2) {
				*outputBuffer += multiply_32x32_rshift32_rounded(rawBuffer[rawBufferReadPos * 2 + 1], amplitude) << 4;
				outputBuffer++;
			}
			rawBufferReadPos = (rawBufferReadPos + 1) & (kInputRawBufferSize - 1);
		} while (outputBuffer != outputBufferEnd);
	}
}

// Returns how much longer (in raw samples) this play-head could play for before it reaches "now" time (which is itself
// moving forward) and runs out of audio Only valid if phaseIncrement > kMaxSampleValue
int32_t LivePitchShifterPlayHead::getEstimatedPlaytimeRemaining(uint32_t repitchedBufferWritePos,
                                                                LiveInputBuffer* liveInputBuffer,
                                                                int32_t phaseIncrement) {
	uint32_t howFarBack;
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		howFarBack = (uint32_t)(repitchedBufferWritePos - repitchedBufferReadPos) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		uint32_t howFarBackRaw =
		    (uint32_t)(liveInputBuffer->numRawSamplesProcessed - rawBufferReadPos) & (kInputRawBufferSize - 1);
		howFarBack = ((uint64_t)howFarBackRaw << 24) / (uint32_t)phaseIncrement;
	}
	else {                 // DIRECT
		return 2147483647; // It'd never run out
	}

	uint64_t estimate = ((uint64_t)howFarBack << 24) / (uint32_t)(phaseIncrement - kMaxSampleValue);
	if (estimate >= 2147483647) {
		return 2147483647;
	}
	else {
		return estimate;
	}
}

int32_t LivePitchShifterPlayHead::getNumRawSamplesBehindInput(LiveInputBuffer* liveInputBuffer,
                                                              LivePitchShifter* livePitchShifter,
                                                              int32_t phaseIncrement) {
#if INPUT_ENABLE_REPITCHED_BUFFER
	if (mode == PlayHeadMode::REPITCHED_BUFFER) {
		uint32_t howFarBackRepitched = (uint32_t)(livePitchShifter->repitchedBufferWritePos - repitchedBufferReadPos)
		                               & (INPUT_REPITCHED_BUFFER_SIZE - 1);
		return ((uint64_t)howFarBackRepitched * phaseIncrement) >> 24;
	}
	else
#endif
	    if (mode == PlayHeadMode::RAW_REPITCHING) {
		return (uint32_t)(liveInputBuffer->numRawSamplesProcessed - rawBufferReadPos) & (kInputRawBufferSize - 1);
	}

	else { // DIRECT
		return 0;
	}
}

void LivePitchShifterPlayHead::fillInterpolationBuffer(LiveInputBuffer* liveInputBuffer, int32_t numChannels) {
	for (int32_t c = 0; c < numChannels; c++) {
		for (int32_t i = 1; i <= kInterpolationMaxNumSamples; i++) {
			int32_t pos = (uint32_t)(rawBufferReadPos - i) & (kInputRawBufferSize - 1);

			interpolationBuffer[c][0][i - 1] = (pos < liveInputBuffer->numRawSamplesProcessed)
			                                       ? liveInputBuffer->rawBuffer[pos * numChannels + c] >> 16
			                                       : 0;
		}
	}
}
