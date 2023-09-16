/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "processing/vector_rendering_function.h"
#include "storage/wave_table/wave_table.h"
#include "util/functions.h"
#include <arm_neon.h>
#include <cstdint>

[[gnu::always_inline]] static inline void //<
renderOscSync(auto storageFunctionName, auto extraInstructionsForCrossoverSampleRedo,
              // Params
              uint32_t phase, uint32_t phaseIncrement, uint32_t& resetterPhase, uint32_t resetterPhaseIncrement,
              uint32_t resetterDivideByPhaseIncrement, uint32_t retriggerPhase, int32_t numSamplesThisOscSyncSession,
              int32_t*& bufferStartThisSync) {

	bool renderedASyncFromItsStartYet = false;
	int32_t crossoverSampleBeforeSync;
	int32_t fadeBetweenSyncs;

	/* Do a bunch of samples until we get to the next crossover sample */

	/* A starting value that'll be added to. It's 1 because we want to include the 1 extra sample at the end - the crossover sample. */
	uint32_t samplesIncludingNextCrossoverSample = 1;
startRenderingASync:
	uint32_t distanceTilNextCrossoverSample = -resetterPhase - (resetterPhaseIncrement >> 1);
	samplesIncludingNextCrossoverSample += (uint32_t)(distanceTilNextCrossoverSample - 1) / resetterPhaseIncrement;
	bool shouldBeginNextSyncAfter = (numSamplesThisOscSyncSession >= samplesIncludingNextCrossoverSample);
	int32_t numSamplesThisSyncRender = shouldBeginNextSyncAfter
	                                       ? samplesIncludingNextCrossoverSample
	                                       : numSamplesThisOscSyncSession; /* Just limit it, basically. */

	int32_t const* const bufferEndThisSyncRender = bufferStartThisSync + numSamplesThisSyncRender;
	uint32_t phaseTemp = phase;
	int32_t* __restrict__ writePos = bufferStartThisSync;

	storageFunctionName(phaseTemp, bufferEndThisSyncRender, writePos);

	/* Sort out the crossover sample at the *start* of that window we just did, if there was one. */
	if (renderedASyncFromItsStartYet) {
		int32_t average = (*bufferStartThisSync >> 1) + (crossoverSampleBeforeSync >> 1);
		int32_t halfDifference = (*bufferStartThisSync >> 1) - (crossoverSampleBeforeSync >> 1);
		int32_t sineValue = getSine(fadeBetweenSyncs >> 1);
		*bufferStartThisSync = average + (multiply_32x32_rshift32(halfDifference, sineValue) << 1);
	}

	if (shouldBeginNextSyncAfter) {
		/* We've just done a crossover (i.e. hit a sync point) at the end of that window, so start thinking about that and planning the next window. */
		bufferStartThisSync += samplesIncludingNextCrossoverSample - 1;
		crossoverSampleBeforeSync = *bufferStartThisSync;
		numSamplesThisOscSyncSession -= samplesIncludingNextCrossoverSample - 1;
		extraInstructionsForCrossoverSampleRedo;

		/* We want this to always show one sample late at this point (why again?). */
		/* The first time we get here, it won't yet be, so make it so. */
		resetterPhase += resetterPhaseIncrement * (samplesIncludingNextCrossoverSample - renderedASyncFromItsStartYet);

		/* The result of that comes out as between "-0.5 and 0.5", represented as +-(1<<14) */
		/* And this makes it "full-scale", so "1" is 1<<32. */
		fadeBetweenSyncs = multiply_32x32_rshift32((int32_t)resetterPhase, resetterDivideByPhaseIncrement) << 17;
		phase = multiply_32x32_rshift32(fadeBetweenSyncs, phaseIncrement) + retriggerPhase;

		phase -= phaseIncrement; /* Because we're going back and redoing the last sample. */
		renderedASyncFromItsStartYet = true;

		/* Make this 1 higher now, because resetterPhase's value is 1 sample later than what it "is in reality". */
		samplesIncludingNextCrossoverSample = 2;

		goto startRenderingASync;
	}

	/* We're not beginning a next sync, so are not going to reset phase, so need to update (increment) it to keep it valid. */
	phase += phaseIncrement * numSamplesThisSyncRender;
}

#define STORE_VECTOR_WAVE_FOR_ONE_SYNC(vectorValueFunction)                                                            \
	[&](uint32_t& phaseTemp, int32_t const* const bufferEndThisSyncRender, int32_t* __restrict__& writePos) {          \
		do {                                                                                                           \
			int32x4_t valueVector;                                                                                     \
			vectorValueFunction(valueVector, phaseTemp, phaseIncrement, phaseToAdd, table, tableSizeMagnitude);        \
			vst1q_s32(writePos, valueVector);                                                                          \
			writePos += 4;                                                                                             \
		} while (writePos < bufferEndThisSyncRender);                                                                  \
	}

template <int i>
[[gnu::always_inline]] static inline void //<
setupAmplitudeVector(int32x4_t& amplitudeVector, int32_t& amplitude, int32_t amplitudeIncrement) {
	amplitude += amplitudeIncrement;
	amplitudeVector = vsetq_lane_s32(amplitude >> 1, amplitudeVector, i);
}

[[gnu::always_inline]] static inline auto //<
setupForApplyingAmplitudeWithVectors(int32x4_t& amplitudeVector, int32x4_t& amplitudeIncrementVector,
                                     int32_t& amplitude, int32_t amplitudeIncrement) {
	setupAmplitudeVector<0>(amplitudeVector, amplitude, amplitudeIncrement);
	setupAmplitudeVector<1>(amplitudeVector, amplitude, amplitudeIncrement);
	setupAmplitudeVector<2>(amplitudeVector, amplitude, amplitudeIncrement);
	setupAmplitudeVector<3>(amplitudeVector, amplitude, amplitudeIncrement);
	amplitudeIncrementVector = vdupq_n_s32(amplitudeIncrement << 1);
}

/* Before calling, you must:
	amplitude <<= 1;
	amplitudeIncrement <<= 1;
*/
#define CREATE_WAVE_RENDER_FUNCTION_INSTANCE(thisFunctionInstanceName)                                                 \
	template <typename FuncType>                                                                                       \
	__attribute__((optimize("unroll-loops"))) void thisFunctionInstanceName(                                           \
	    FuncType vectorValueFunction, const int16_t* __restrict__ table, int32_t tableSizeMagnitude,                   \
	    int32_t amplitude, int32_t* __restrict__ outputBuffer, int32_t* bufferEnd, uint32_t phaseIncrement,            \
	    uint32_t phase, bool applyAmplitude, uint32_t phaseToAdd, int32_t amplitudeIncrement) {                        \
                                                                                                                       \
		int32_t* __restrict__ outputBufferPos = outputBuffer;                                                          \
                                                                                                                       \
		int32x4_t amplitudeVector;                                                                                     \
		int32x4_t amplitudeIncrementVector;                                                                            \
                                                                                                                       \
		setupForApplyingAmplitudeWithVectors(amplitudeVector, amplitudeIncrementVector, amplitude,                     \
		                                     amplitudeIncrement);                                                      \
		uint32_t phaseTemp = phase;                                                                                    \
                                                                                                                       \
		do {                                                                                                           \
                                                                                                                       \
			int32x4_t valueVector;                                                                                     \
			vectorValueFunction(valueVector, phaseTemp, phaseIncrement, phaseToAdd, table, tableSizeMagnitude);        \
                                                                                                                       \
			if (applyAmplitude) {                                                                                      \
				int32x4_t existingDataInBuffer = vld1q_s32(outputBufferPos);                                           \
				valueVector = vqdmulhq_s32(amplitudeVector, valueVector);                                              \
				amplitudeVector = vaddq_s32(amplitudeVector, amplitudeIncrementVector);                                \
				valueVector = vaddq_s32(valueVector, existingDataInBuffer);                                            \
			}                                                                                                          \
                                                                                                                       \
			vst1q_s32(outputBufferPos, valueVector);                                                                   \
                                                                                                                       \
			outputBufferPos += 4;                                                                                      \
		} while (outputBufferPos < bufferEnd);                                                                         \
	};
