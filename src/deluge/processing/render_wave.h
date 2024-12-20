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
#include "util/fixedpoint.h"
#include "util/waves.h"
#include <argon.hpp>
#include <arm_neon.h>

[[gnu::always_inline]] static inline void //<
renderOscSync(auto storageFunction, auto extraInstructionsForCrossoverSampleRedo,
              // Params
              uint32_t& phase, uint32_t phaseIncrement, uint32_t& resetterPhase, uint32_t resetterPhaseIncrement,
              int32_t resetterDivideByPhaseIncrement, uint32_t retriggerPhase, int32_t& numSamplesThisOscSyncSession,
              int32_t*& bufferStartThisSync) {

	bool renderedASyncFromItsStartYet = false;
	int32_t crossoverSampleBeforeSync;
	int32_t fadeBetweenSyncs;

	/* Do a bunch of samples until we get to the next crossover sample */
	/* A starting value that'll be added to. It's 1 because we want to include the 1 extra sample at the end - the
	 * crossover sample. */
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

	storageFunction(bufferEndThisSyncRender, phaseTemp, writePos);

	/* Sort out the crossover sample at the *start* of that window we just did, if there was one. */
	if (renderedASyncFromItsStartYet) {
		int32_t average = (*bufferStartThisSync >> 1) + (crossoverSampleBeforeSync >> 1);
		int32_t halfDifference = (*bufferStartThisSync >> 1) - (crossoverSampleBeforeSync >> 1);
		int32_t sineValue = getSine(fadeBetweenSyncs >> 1);
		*bufferStartThisSync = average + (multiply_32x32_rshift32(halfDifference, sineValue) << 1);
	}

	if (shouldBeginNextSyncAfter) {
		/* We've just done a crossover (i.e. hit a sync point) at the end of that window, so start thinking about that
		 * and planning the next window. */
		bufferStartThisSync += samplesIncludingNextCrossoverSample - 1;
		crossoverSampleBeforeSync = *bufferStartThisSync;
		numSamplesThisOscSyncSession -= samplesIncludingNextCrossoverSample - 1;
		extraInstructionsForCrossoverSampleRedo(samplesIncludingNextCrossoverSample);

		/* We want this to always show one sample late at this point (why again?). */
		resetterPhase += resetterPhaseIncrement * (samplesIncludingNextCrossoverSample - renderedASyncFromItsStartYet);
		/* The first time we get here, it won't yet be, so make it so. */

		/* The result of that comes out as between "-0.5 and 0.5", represented as +-(1<<14) */
		fadeBetweenSyncs = multiply_32x32_rshift32((int32_t)resetterPhase, resetterDivideByPhaseIncrement)
		                   << 17; /* And this makes it "full-scale", so "1" is 1<<32. */
		phase = multiply_32x32_rshift32(fadeBetweenSyncs, phaseIncrement) + retriggerPhase;

		phase -= phaseIncrement; /* Because we're going back and redoing the last sample. */
		renderedASyncFromItsStartYet = true;
		samplesIncludingNextCrossoverSample = 2; /* Make this 1 higher now, because resetterPhase's value is 1 sample
		                                            later than what it "is in reality". */
		goto startRenderingASync;
	}

	/* We're not beginning a next sync, so are not going to reset phase, so need to update (increment) it to keep it
	 * valid. */
	phase += phaseIncrement * numSamplesThisSyncRender;
}

auto renderWavetableLoop(auto bufferStartThisSync, auto firstCycleNumber, auto bandHere, auto phaseIncrement,
                         auto crossCycleStrength2, auto crossCycleStrength2Increment, auto kernel) {
	return [&](int32_t const* const bufferEndThisSyncRender, uint32_t phaseTemp, int32_t* __restrict__ writePos) {
		doRenderingLoop(bufferStartThisSync, bufferEndThisSyncRender, firstCycleNumber, bandHere, phaseTemp,
		                phaseIncrement, crossCycleStrength2, crossCycleStrength2Increment, kernel);
	};
}

auto storeVectorWaveForOneSync(auto vectorValueFunction) {
	return [&](int32_t const* const bufferEndThisSyncRender, uint32_t phaseTemp, int32_t* __restrict__ writePos) {
		do {
			int32x4_t valueVector;
			vectorValueFunction();
			vst1q_s32(writePos, valueVector);
			writePos += 4;
		} while (writePos < bufferEndThisSyncRender);
	};
}

/// @note amplitude and amplitudeIncrement multiplied by two before being passed to this function
inline int32x4_t createAmplitudeVector(int32_t amplitude, int32_t amplitudeIncrement) {
	int32x4_t amplitudeVector = vld1q_dup_s32(&amplitude);
	int32x4_t incrementVector = vld1q_dup_s32(&amplitudeIncrement);

	// amplitude + amplitudeIncrement * lane_n
	amplitudeVector = vmlaq_s32(amplitudeVector, incrementVector, int32x4_t{1, 2, 3, 4});

	// TODO(@stellar-aria): investigate where the doubling comes from (likely an unshifted smmul)
	return vshrq_n_s32(amplitudeVector, 1); // halve amplitude
}
