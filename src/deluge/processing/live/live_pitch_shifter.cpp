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

#include "definitions_cxx.hpp"
#include "processing/engines/audio_engine.h"
#include "processing/live/live_pitch_shifter.h"
#include "processing/live/live_input_buffer.h"
#include "memory/general_memory_allocator.h"
#include "storage/storage_manager.h"
#include "dsp/timestretch/time_stretcher.h"
#include <stdlib.h>
#include "io/debug/print.h"
#include "util/functions.h"

//#define MEASURE_HOP_END_PERFORMANCE 1

LivePitchShifter::LivePitchShifter(OscType newInputType, int32_t phaseIncrement) {
	inputType = newInputType;
	numChannels = (newInputType == OscType::INPUT_STEREO) ? 2 : 1;

	if (phaseIncrement < 16777216) {
		nextCrossfadeLength = samplesTilHopEnd = INTERPOLATION_MAX_NUM_SAMPLES * 2;
	}

	else if (phaseIncrement < 17774841) { // If up by less than 1 semitone...
		samplesTilHopEnd = 2048;
		nextCrossfadeLength = 256;
	}

	else {
		nextCrossfadeLength = samplesTilHopEnd = 256;
	}

	crossfadeProgress = 16777216;
	samplesIntoHop = 0;

#if INPUT_ENABLE_REPITCHED_BUFFER
	stillWritingToRepitchedBuffer = false;
	repitchedBuffer = NULL;
#endif
	considerRepitchedBuffer(phaseIncrement);

	playHeads[PLAY_HEAD_NEWER].mode = PLAY_HEAD_MODE_RAW_DIRECT;
	playHeads[PLAY_HEAD_NEWER].oscPos = 0;

	playHeads[PLAY_HEAD_NEWER].rawBufferReadPos = 0;
	playHeads[PLAY_HEAD_NEWER].percPos = 0xFFFFFFFF;
}

LivePitchShifter::~LivePitchShifter() {

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer) {
		generalMemoryAllocator.dealloc(repitchedBuffer);
	}
#endif
}

void LivePitchShifter::render(int32_t* __restrict__ outputBuffer, int numSamplesThisFunctionCall,
                              int32_t phaseIncrement, int32_t amplitude, int32_t amplitudeIncrement,
                              int interpolationBufferSize) {

	LiveInputBuffer* liveInputBuffer = AudioEngine::getOrCreateLiveInputBuffer(inputType, false);
	if (ALPHA_OR_BETA_VERSION && !liveInputBuffer) {
		numericDriver.freezeWithError("E165");
	}

	liveInputBuffer->giveInput(numSamplesThisFunctionCall, AudioEngine::audioSampleTimer, inputType);

	int whichKernel = getWhichKernel(phaseIncrement);

	uint64_t numRawSamplesProcessedAtStart = liveInputBuffer->numRawSamplesProcessed - numSamplesThisFunctionCall;

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer) {
		for (int i = 0; i < numSamplesThisFunctionCall; i++) {

			// Shift contents of interpolation buffer along by 1
			for (int b = INTERPOLATION_MAX_NUM_SAMPLES - 1; b >= 1; b--) {
				interpolationBuffer[0][b] = interpolationBuffer[0][b - 1];
				if (numChannels == 2) {
					interpolationBuffer[1][b] = interpolationBuffer[1][b - 1];
				}
			}

			// Feed this 1 input sample into interpolation buffer
			StereoSample* inputSample = audioDriver.getInputSample(i);

			interpolationBuffer[0][0] = (inputType == OscType::INPUT_R) ? inputSample->r : inputSample->l;

			if (numChannels == 2) {
				interpolationBuffer[1][0] = inputSample->r;
			}

			while (oscPos < 16777216) {

				// Interpolate and put in buffer
				interpolate(&repitchedBuffer[repitchedBufferWritePos * numChannels], interpolationBufferSize,
				            numChannels, whichKernel);

				repitchedBufferWritePos = (repitchedBufferWritePos + 1) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
				repitchedBufferNumSamplesWritten++;

				oscPos += phaseIncrement;
			}

			oscPos -= 16777216;
		}
	}
#endif

	bool justDidHop = false;

startRenderAgain:

	if (!justDidHop && phaseIncrement > 16777216) {
		int maxPlayableSamplesNewer = playHeads[PLAY_HEAD_NEWER].getEstimatedPlaytimeRemaining(
#if INPUT_ENABLE_REPITCHED_BUFFER
		    repitchedBufferNumSamplesWritten,
#else
		    0,
#endif
		    liveInputBuffer, phaseIncrement);

		if (samplesTilHopEnd + nextCrossfadeLength > maxPlayableSamplesNewer) {

			int32_t maxTotalPlayable = maxPlayableSamplesNewer + samplesIntoHop;
			nextCrossfadeLength = getMin(nextCrossfadeLength, maxTotalPlayable >> 1);

			samplesTilHopEnd = maxPlayableSamplesNewer - nextCrossfadeLength;
			//Debug::println("shortening hop");

			if (samplesTilHopEnd < 0) {
				samplesTilHopEnd = 0;
				nextCrossfadeLength = getMax(maxPlayableSamplesNewer, 0);
				//Debug::println("nex");
				crossfadeProgress = 16777216;
			}

			else if (samplesTilHopEnd == 0) {
				//Debug::println("to zero");
			}

			else if (samplesTilHopEnd > 0 && olderPlayHeadIsCurrentlySounding()) {
				uint32_t minCrossfadeIncrement = (uint32_t)(16777216 - crossfadeProgress) / samplesTilHopEnd + 1;
				if (minCrossfadeIncrement > crossfadeIncrement) {
					crossfadeIncrement = minCrossfadeIncrement;
					//Debug::println("d");
				}
			}
		}

		if (samplesTilHopEnd && olderPlayHeadIsCurrentlySounding()) {
			uint32_t maxPlayableSamplesOlder = playHeads[PLAY_HEAD_OLDER].getEstimatedPlaytimeRemaining(
#if INPUT_ENABLE_REPITCHED_BUFFER
			    repitchedBufferNumSamplesWritten,
#else
			    0,
#endif
			    liveInputBuffer, phaseIncrement);
			//Debug::println(maxPlayableSamplesOlder);
			if (!maxPlayableSamplesOlder) {
				crossfadeIncrement = 16777216;
			}
			else {
				uint32_t minCrossfadeIncrement = (uint32_t)(16777216 - crossfadeProgress) / maxPlayableSamplesOlder + 1;
				//crossfadeIncrement = getMax(crossfadeIncrement, minCrossfadeIncrement);
				if (minCrossfadeIncrement > crossfadeIncrement) {
					crossfadeIncrement = minCrossfadeIncrement;
					//Debug::println("c");
				}
			}
		}
	}

#if !MEASURE_HOP_END_PERFORMANCE
	// If the percussiveness is higher at "now" time than at the newer play head, end the hop.
	// This was designed with pitching-down in mind, but sounds good on pitching-up too
	if (!justDidHop && !olderPlayHeadIsCurrentlySounding() && samplesTilHopEnd
	    && playHeads[PLAY_HEAD_NEWER].mode != PLAY_HEAD_MODE_RAW_DIRECT) {

		int howFarBack = playHeads[PLAY_HEAD_NEWER].getNumRawSamplesBehindInput(liveInputBuffer, this, phaseIncrement);

		uint32_t newerPlayHeadPercPos =
		    (liveInputBuffer->numRawSamplesProcessed - howFarBack - 1) >> PERC_BUFFER_REDUCTION_MAGNITUDE;

		uint32_t latestPercPosBefore = (numRawSamplesProcessedAtStart - 1) >> PERC_BUFFER_REDUCTION_MAGNITUDE;
		uint32_t latestPercPosNow = (liveInputBuffer->numRawSamplesProcessed - 1) >> PERC_BUFFER_REDUCTION_MAGNITUDE;

		if (latestPercPosNow != newerPlayHeadPercPos
		    && (newerPlayHeadPercPos != playHeads[PLAY_HEAD_NEWER].percPos
		        || latestPercPosNow != latestPercPosBefore)) {

			int percLatest = liveInputBuffer->percBuffer[latestPercPosNow & (INPUT_PERC_BUFFER_SIZE - 1)];
			int percNewerPlayHead = liveInputBuffer->percBuffer[newerPlayHeadPercPos & (INPUT_PERC_BUFFER_SIZE - 1)];

			if (percLatest >= percNewerPlayHead + percThresholdForCut) {
				/*
				Debug::print(percLatest);
				Debug::print(" vs ");
				Debug::println(percNewerPlayHead);
				*/
				samplesTilHopEnd = 0;
			}
		}

		playHeads[PLAY_HEAD_NEWER].percPos = newerPlayHeadPercPos;
	}
#endif

	if (!samplesTilHopEnd) {
		hopEnd(phaseIncrement, liveInputBuffer, numRawSamplesProcessedAtStart, liveInputBuffer->numRawSamplesProcessed);
		justDidHop = true;
		goto startRenderAgain;
	}

	int numSamplesThisTimestretchedRead = getMin((int32_t)numSamplesThisFunctionCall, samplesTilHopEnd);

	bool olderPlayHeadAudibleHere = olderPlayHeadIsCurrentlySounding();

	int32_t newerSourceAmplitudeNow;
	int32_t newerAmplitudeIncrementNow;
	int32_t olderSourceAmplitudeNow;
	int32_t olderAmplitudeIncrementNow;

	// If the older play-head is still fading out, which means we need to work out the crossfade envelope too...
	if (olderPlayHeadAudibleHere) {

		// Use linear crossfades. These sound less jarring when doing short hops. Square root ones sound better for longer hops, with more difference in material between hops (i.e. time more stretched)
		int32_t newerHopAmplitudeNow = crossfadeProgress << 7;
		int32_t olderHopAmplitudeNow = 2147483647 - newerHopAmplitudeNow;

		crossfadeProgress += crossfadeIncrement * numSamplesThisTimestretchedRead;

		int32_t newerHopAmplitudeAfter = lshiftAndSaturate<7>(crossfadeProgress);

		int32_t newerHopAmplitudeIncrement =
		    (int32_t)(newerHopAmplitudeAfter - newerHopAmplitudeNow) / (int32_t)numSamplesThisTimestretchedRead;
		int32_t olderHopAmplitudeIncrement = -newerHopAmplitudeIncrement;

		int32_t hopAmplitudeChange = multiply_32x32_rshift32(amplitude, newerHopAmplitudeIncrement) << 1;

		newerAmplitudeIncrementNow = amplitudeIncrement + hopAmplitudeChange;
		newerSourceAmplitudeNow = multiply_32x32_rshift32(amplitude, newerHopAmplitudeNow) << 1;

		olderAmplitudeIncrementNow = amplitudeIncrement - hopAmplitudeChange;
		olderSourceAmplitudeNow = multiply_32x32_rshift32(amplitude, olderHopAmplitudeNow) << 1;
	}

	// Or, if we're just hearing the newer play-head
	else {
		newerSourceAmplitudeNow = amplitude;
		newerAmplitudeIncrementNow = amplitudeIncrement;
		// No need to set the "older" ones, cos that's not going to be rendered
	}

	// Read newer play-head
	playHeads[PLAY_HEAD_NEWER].render(outputBuffer, numSamplesThisTimestretchedRead, numChannels, phaseIncrement,
	                                  newerSourceAmplitudeNow, newerAmplitudeIncrementNow,
#if INPUT_ENABLE_REPITCHED_BUFFER
	                                  repitchedBuffer,
#else
	                                  NULL,
#endif
	                                  liveInputBuffer->rawBuffer, whichKernel, interpolationBufferSize);

	// Read older play-head
	if (olderPlayHeadAudibleHere) {
		playHeads[PLAY_HEAD_OLDER].render(outputBuffer, numSamplesThisTimestretchedRead, numChannels, phaseIncrement,
		                                  olderSourceAmplitudeNow, olderAmplitudeIncrementNow,
#if INPUT_ENABLE_REPITCHED_BUFFER
		                                  repitchedBuffer,
#else
		                                  NULL,
#endif
		                                  liveInputBuffer->rawBuffer, whichKernel, interpolationBufferSize);
	}

	samplesTilHopEnd -= numSamplesThisTimestretchedRead;
	samplesIntoHop += numSamplesThisTimestretchedRead;
	numSamplesThisFunctionCall -= numSamplesThisTimestretchedRead;

	if (numSamplesThisFunctionCall) {
		outputBuffer += numSamplesThisTimestretchedRead * numChannels;

		amplitude += amplitudeIncrement * numSamplesThisTimestretchedRead;

		numRawSamplesProcessedAtStart += numSamplesThisTimestretchedRead;

		goto startRenderAgain;
	}
}

const int16_t minSearchFine[] = {
    8, 8, 8, 8, 8, 8,  8,  8,  // -12, ....
    8, 8, 8, 8, 8, 12, 14, 16, // +0, ....
    17                         // +12
};

const int16_t minSearchCoarse[] = {10, 10, 10, 17, 20};

const int16_t maxSearchFine[] = {
    8, 8, 8,  8,  8,  8,  8,  8,  // -12, ....
    8, 8, 10, 11, 12, 14, 16, 18, // +0, ....
    21                            // +12
};

const int16_t maxSearchCoarse[] = {15, 15, 15, 21, 20};

const int16_t percThresholdFine[] = {
    18,  18, 18, 18, 24, 30, 25, 35, // -12, ....
    130, 40, 40, 45, 50, 47, 45, 42, // +0, ....
    40                               // +12
};

const int16_t percThresholdCoarse[] = {15, 18, 130, 40, 20};

const int16_t crossfadeFine[] = {
    30, 31, 32, 34, 35, 30, 25, 10, // -12, ....
    10, 15, 15, 22, 30, 16, 15, 13, // +0, ....
    40                              // +12
};

const int16_t crossfadeCoarse[] = {30, 30, 10, 40, 20};

const int16_t maxHopLengthFine[] = {
    20,  27,  35,  42,  50,  60,  70,  90,  // -12, ....
    140, 140, 140, 140, 140, 140, 140, 140, // +0, ....
    140                                     // +12
};

const int16_t maxHopLengthCoarse[] = {10, 20, 140, 140, 140};

const int16_t randomFine[] = {
    0, 0,  0,  0,  0,  0,  0,  0,  // -12, ....
    0, 15, 25, 76, 50, 57, 65, 72, // +0, ....
    80                             // +12
};

const int16_t randomCoarse[] = {0, 0, 0, 80, 80};

void LivePitchShifter::hopEnd(int32_t phaseIncrement, LiveInputBuffer* liveInputBuffer,
                              uint64_t numRawSamplesProcessedAtNowTime, uint64_t numRawSamplesProcessedLatest) {

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t startTime = MTU2.TCNT_0;
#endif

	AudioEngine::numHopsEndedThisRoutineCall++;

	//int numChannelsNow = numChannels;

	//Debug::println("");
	//Debug::print("hop at ");
	//Debug::println(numRawSamplesProcessedAtNowTime);
	if (crossfadeProgress < 16777216) {
		Debug::println("last crossfade not finished");
		//if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("FADE");
	}
	//Debug::println(phaseIncrement);

	//Debug::println((uint32_t)(liveInputBuffer->numRawSamplesProcessed - playHeads[PLAY_HEAD_NEWER].rawBufferReadPos) & (INPUT_RAW_BUFFER_SIZE - 1)); // How far behind raw buffer is being read

	// What was new is now old
	playHeads[PLAY_HEAD_OLDER] = playHeads[PLAY_HEAD_NEWER];
	uint32_t thisCrossfadeLength = nextCrossfadeLength;

	int pitchLog = quickLog(phaseIncrement);

	int minSearch;
	int maxSearch;
	int maxHopLength;
	int randomElement;

	// Neutral is (832 << 20). Each octave is a (32 << 20)

	// If within +/- 1 octave...
	if (pitchLog >= (800 << 20) && pitchLog < (864 << 20)) {
		int position = pitchLog - (800 << 20);

		minSearch = interpolateTableSigned(position, 26, minSearchFine, 4) >> 9;
		maxSearch = interpolateTableSigned(position, 26, maxSearchFine, 4) >> 9;
		percThresholdForCut = interpolateTableSigned(position, 26, percThresholdFine, 4) >> 16;
		nextCrossfadeLength = interpolateTableSigned(position, 26, crossfadeFine, 4) >> 12;
		maxHopLength = (interpolateTableSigned(position, 26, maxHopLengthFine, 4) >> 16) * 100;
		randomElement = interpolateTableSigned(position, 26, randomFine, 4);
	}

	// Or if outside of that...
	else {
		if (pitchLog > (896 << 20)) {
			pitchLog = (896 << 20);
		}
		else if (pitchLog < (768 << 20)) {
			pitchLog = (768 << 20);
		}

		int position = pitchLog - (768 << 20);

		minSearch = interpolateTableSigned(position, 27, minSearchCoarse, 2) >> 9;
		maxSearch = interpolateTableSigned(position, 27, maxSearchCoarse, 2) >> 9;
		percThresholdForCut = interpolateTableSigned(position, 27, percThresholdCoarse, 2) >> 16;
		nextCrossfadeLength = interpolateTableSigned(position, 27, crossfadeCoarse, 2) >> 12;
		maxHopLength = (interpolateTableSigned(position, 27, maxHopLengthCoarse, 2) >> 16) * 100;
		randomElement = interpolateTableSigned(position, 27, randomCoarse, 2);
	}

	/*
	minSearch = storageManager.devVarA << 7;
	maxSearch = storageManager.devVarB << 7;
	nextCrossfadeLength = storageManager.devVarC << 4;
	percThresholdForCut = storageManager.devVarD;
	maxHopLength = storageManager.devVarE * 100;
	//int searchSizeFactor = storageManager.devVarF;
	randomElement = storageManager.devVarG << 16;
	*/

	// Collect info on those moving average for the now-older play-head, which we're going to fade out -
	// so that we can then fine-tune the position of the new play-head to match it

	// First, work out the length we'd *like* to use for the moving averages
	int lengthPerMovingAverage = ((uint64_t)phaseIncrement * TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH) >> 24;
	lengthPerMovingAverage = getMax(lengthPerMovingAverage, 1);
	lengthPerMovingAverage =
	    getMin(lengthPerMovingAverage, TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH * 2); // Keep things sensible

	// Ok, and this crossfade we're about to do, how long will it be in samples of (unpitched) source material?
	int crossfadeLengthSamplesSource = ((uint64_t)thisCrossfadeLength * phaseIncrement) >> 24;

	// What's the maximum amount further forward than that older play-head that data actually exists yet for us to examine?
	int maxOffsetFromHead = (uint32_t)(numRawSamplesProcessedLatest - playHeads[PLAY_HEAD_OLDER].rawBufferReadPos)
	                        & (INPUT_RAW_BUFFER_SIZE - 1);

	// Ok, work out the end-pos of our moving-averages region
	int averagesEndOffsetFromHead = (crossfadeLengthSamplesSource >> 1)
	                                + ((lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES) >> 1);
	averagesEndOffsetFromHead = getMin(averagesEndOffsetFromHead,
	                                   maxOffsetFromHead); // And make sure it's not beyond the end of the existent data

	// We now know the length of the *total* moving-averages region, so divide down to get the length of *each* moving-average region
	// If commenting out this next line, must make sure we still don't search back before we started writing to buffer
	lengthPerMovingAverage =
	    getMin(lengthPerMovingAverage, averagesEndOffsetFromHead >> 1); // / TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES

	int averagesStartOffsetFromHead =
	    averagesEndOffsetFromHead - (lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES);

	int32_t oldHeadTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];

	// Occasionally (if we've only just switched pitch shifting on), that'll be zero, so we can'd do the moving averages.
	// But if it's ok, then...
	if (lengthPerMovingAverage) {
		int averagesStartPosOldHead =
		    (playHeads[PLAY_HEAD_OLDER].rawBufferReadPos + averagesStartOffsetFromHead) & (INPUT_RAW_BUFFER_SIZE - 1);
		liveInputBuffer->getAveragesForCrossfade(oldHeadTotals, averagesStartPosOldHead, lengthPerMovingAverage,
		                                         numChannels);
	}

	int averagesStartPosNewHead;
	int32_t searchSize;
	int searchDirection;
	int numFullDirectionsSearched;

	int howFarBack;

	// Now we're going to pick a position for the new play-head based on searching the percussiveness data. This is totally different for pitching up vs down.
	// And while we're at it, also decide our search parameters for our fine-tuning of the new play head pos - if we're going to do that (we might not - if lengthPerMovingAverage == 0)

	// If pitch shifting up...
	if (phaseIncrement > 16777216) {

		if (liveInputBuffer) {

			// Search backwards, looking for the area (ending at now-time) with the highest average percussiveness.
			// This particularly helps to keep the hops from all happening at the same length, which leads to a clearly audible sort of "tone".
			// But, some sort of random element could actually work just as well - I haven't tried yet.

#if !MEASURE_HOP_END_PERFORMANCE
			minSearch +=
			    (multiply_32x32_rshift32(minSearch, multiply_32x32_rshift32(getNoise(), randomElement << 8)) << 2);
#endif

			int backEdge = minSearch >> PERC_BUFFER_REDUCTION_MAGNITUDE; // Pixellated
			int howFarBackSearched = 0;
			int percPos =
			    (numRawSamplesProcessedAtNowTime + PERC_BUFFER_REDUCTION_SIZE - 1) >> PERC_BUFFER_REDUCTION_MAGNITUDE;

			uint32_t totalPerc = 0;
			float bestAverage = 0;
			int bestHowFarBack = minSearch >> PERC_BUFFER_REDUCTION_MAGNITUDE; // Pixellated

			while (backEdge < (maxSearch >> PERC_BUFFER_REDUCTION_MAGNITUDE)) {

				while (howFarBackSearched < backEdge) {
					howFarBackSearched++;
					if (howFarBackSearched > percPos) {
						goto stopPercSearch;
					}
					int percHere =
					    liveInputBuffer
					        ->percBuffer[(uint32_t)(percPos - howFarBackSearched) & (PERC_BUFFER_REDUCTION_SIZE - 1)];
					totalPerc += percHere;
				}

				float averagePerc = (float)totalPerc / howFarBackSearched;
				if (averagePerc > bestAverage) {
					bestAverage = averagePerc;
					bestHowFarBack = howFarBackSearched;
				}

				//else if (averagePerc < bestAverage * 0.5) break; // Didn't help

				backEdge++;
			}

stopPercSearch:

			howFarBack = bestHowFarBack << PERC_BUFFER_REDUCTION_MAGNITUDE;
		}

		else {
			howFarBack = 1500;
		}

		samplesTilHopEnd = ((uint64_t)howFarBack << 24) / (uint32_t)(phaseIncrement - 16777216) - nextCrossfadeLength;
		if (samplesTilHopEnd < 100) {
			samplesTilHopEnd =
			    100; // Must be 100, not 200. Otherwise, shifting up 2 octaves gets messed up. (Though maybe not anymore?)
		}
		else if (samplesTilHopEnd > maxHopLength) {
			samplesTilHopEnd = maxHopLength;
		}

		int minDistanceBack = numRawSamplesProcessedAtNowTime - numRawSamplesProcessedLatest
		                      + averagesStartOffsetFromHead
		                      + (lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES);
		howFarBack = getMax(howFarBack, minDistanceBack);

		if (howFarBack > numRawSamplesProcessedAtNowTime) {
			howFarBack = numRawSamplesProcessedAtNowTime;
		}

		if (lengthPerMovingAverage) {
			averagesStartPosNewHead = (numRawSamplesProcessedAtNowTime - howFarBack + averagesStartOffsetFromHead)
			                          & (INPUT_RAW_BUFFER_SIZE - 1);
			searchSize = 490; // Allow tracking down to about 45Hz
#if !MEASURE_HOP_END_PERFORMANCE
			searchSize = getMin(searchSize, samplesTilHopEnd);
#endif
			numFullDirectionsSearched = 0;
			searchDirection = 1;
		}

		playHeads[PLAY_HEAD_NEWER].rawBufferReadPos =
		    (uint32_t)(numRawSamplesProcessedAtNowTime - howFarBack) & (INPUT_RAW_BUFFER_SIZE - 1);
	}

	// Or if pitch shifting down...
	else {
		samplesTilHopEnd = maxHopLength;

		if (lengthPerMovingAverage) {
			averagesStartPosNewHead =
			    (numRawSamplesProcessedLatest - lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES)
			    & (INPUT_RAW_BUFFER_SIZE - 1);
			searchSize = 980; // Allow tracking down to about 45Hz
#if !MEASURE_HOP_END_PERFORMANCE
			searchSize = getMin(searchSize, samplesIntoHop);
#endif
			numFullDirectionsSearched = 1;
			searchDirection = -1;
		}

		playHeads[PLAY_HEAD_NEWER].rawBufferReadPos =
		    (uint32_t)(numRawSamplesProcessedLatest
		               - (lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES)
		               - averagesStartOffsetFromHead)
		    & (INPUT_RAW_BUFFER_SIZE - 1);
	}

	int32_t bestOffset = 0;

	int32_t additionalOscPos = 0;

	// If we're all good to do the moving averages...
	if (lengthPerMovingAverage && playHeads[PLAY_HEAD_OLDER].mode != PLAY_HEAD_MODE_RAW_DIRECT) {

		// Ok, and now we're going to get that moving-average info for the new play-head in the position where we're currently proposing we put it -
		// but we're then going to experiment with shifting that, below

		if (((uint32_t)(averagesStartPosNewHead - averagesStartOffsetFromHead) & (INPUT_RAW_BUFFER_SIZE - 1))
		    >= numRawSamplesProcessedLatest) {
			goto stopSearch;
		}

		if ((uint32_t)(numRawSamplesProcessedLatest - averagesStartPosNewHead)
		    & (INPUT_RAW_BUFFER_SIZE - 1) < lengthPerMovingAverage * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES) {
			goto stopSearch;
		}

		int32_t newHeadTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];
		liveInputBuffer->getAveragesForCrossfade(newHeadTotals, averagesStartPosNewHead, lengthPerMovingAverage,
		                                         numChannels);

		int32_t bestDifferenceAbs = getTotalDifferenceAbs(oldHeadTotals, newHeadTotals);
		int timesSignFlipped = 0;

		int32_t initialTotalChange = getTotalChange(oldHeadTotals, newHeadTotals);

startSearch:
		int32_t lastTotalChange = initialTotalChange;

		int readPos[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1];

		readPos[0] = averagesStartPosNewHead;
		if (searchDirection == -1) {
			readPos[0] = (uint32_t)(readPos[0] - 1) & (INPUT_RAW_BUFFER_SIZE - 1);
		}

		int32_t newHeadRunningTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];
		for (int i = 0; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES; i++) {
			newHeadRunningTotals[i] = newHeadTotals[i];
			readPos[i + 1] = (uint32_t)(readPos[i] + lengthPerMovingAverage) & (INPUT_RAW_BUFFER_SIZE - 1);
		}

		int offsetNow = 0;
		int endOffset;

		int searchSizeBoundary = searchSize;
		if (searchDirection == -1) {
			if (numRawSamplesProcessedLatest < INPUT_RAW_BUFFER_SIZE) { // Is this right?
				searchSizeBoundary =
				    averagesStartPosNewHead - averagesStartOffsetFromHead
				    - 1; // The -1 is becacuse of the 1 we subtract from readPos[0] above when searching left
				if (searchSizeBoundary <= 0) {
					goto searchNextDirection;
				}
			}
		}
		else {
			searchSizeBoundary =
			    (uint32_t)(numRawSamplesProcessedLatest - readPos[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1])
			    & (INPUT_RAW_BUFFER_SIZE - 1);
		}

		{
			int searchSizeHere = getMin(searchSize, searchSizeBoundary);
			endOffset = searchSizeHere * searchDirection;
		}

		do {

			for (int i = 0; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1; i++) {

				int32_t readValue = liveInputBuffer->rawBuffer[readPos[i] * numChannels] >> 16;
				if (numChannels == 2) {
					readValue += liveInputBuffer->rawBuffer[readPos[i] * 2 + 1] >> 16;
				}

				readPos[i] = (uint32_t)(readPos[i] + searchDirection) & (INPUT_RAW_BUFFER_SIZE - 1);

				if (i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES) {
					newHeadRunningTotals[i] -= readValue * searchDirection;
				}

				if (i > 0) {
					newHeadRunningTotals[i - 1] += readValue * searchDirection;
				}
			}

			int32_t differenceAbs = getTotalDifferenceAbs(oldHeadTotals, newHeadRunningTotals);

			// If our very first read is worse, let's switch search direction right now - that'll improve our odds
			if (offsetNow == 0 && searchDirection == 1 && !numFullDirectionsSearched
			    && differenceAbs > bestDifferenceAbs) {
				searchDirection = -searchDirection;
				goto startSearch;
			}

			int newOffsetNow = offsetNow + searchDirection;

			// Keep track of best match
			bool thisOffsetIsBestMatch = (differenceAbs < bestDifferenceAbs);
			if (thisOffsetIsBestMatch) {
				bestDifferenceAbs = differenceAbs;
				bestOffset = newOffsetNow;
			}

			int32_t thisTotalChange = getTotalChange(oldHeadTotals, newHeadRunningTotals);

			// If sign just flipped...
			if ((thisTotalChange >= 0) != (lastTotalChange >= 0)) {

				// Try going in between the samples for the most accurate positioning, lining-up-wise.
				// The benefit of this is visible on a spectrum analysis if you're pitching a high-pitched sine wave right down, while also time stretching it
				if (phaseIncrement != 16777216
				    && (thisOffsetIsBestMatch || offsetNow == bestOffset)) { // If best was this one or last one
					uint32_t thisTotalDifferenceAbs = std::abs(thisTotalChange);
					uint32_t lastTotalDifferenceAbs = std::abs(lastTotalChange);
					additionalOscPos = ((uint64_t)lastTotalDifferenceAbs << 24)
					                   / (uint32_t)(lastTotalDifferenceAbs + thisTotalDifferenceAbs);
					if (searchDirection == -1) {
						additionalOscPos = 16777216 - additionalOscPos;
					}
					if (thisOffsetIsBestMatch != (searchDirection == -1)) {
						bestOffset--;
					}
				}

				// After sign has flipped twice (in total, including both search directions), we can be fairly sure we've found a good fit
				timesSignFlipped++;
#if !MEASURE_HOP_END_PERFORMANCE
				if (timesSignFlipped >= 2) {
					goto stopSearch;
				}
#endif
			}

			offsetNow = newOffsetNow;
			lastTotalChange = thisTotalChange;
		} while (offsetNow != endOffset);

searchNextDirection:
		// Search the other direction if we haven't already
		numFullDirectionsSearched++;
		if (numFullDirectionsSearched < 2) {
			searchDirection = -searchDirection;
			goto startSearch;
		}
	}

stopSearch:

	additionalOscPos += playHeads[PLAY_HEAD_OLDER].oscPos;
	if (additionalOscPos >= 16777216) {
		additionalOscPos -= 16777216;
		bestOffset++;
	}

	playHeads[PLAY_HEAD_NEWER].rawBufferReadPos =
	    (uint32_t)(playHeads[PLAY_HEAD_NEWER].rawBufferReadPos + bestOffset) & (INPUT_RAW_BUFFER_SIZE - 1);

#if INPUT_ENABLE_REPITCHED_BUFFER
	// If pitching up, use repitched buffer if possible
	if (stillWritingToRepitchedBuffer && repitchedBufferNumSamplesWritten && phaseIncrement > 16777216) {
		int howFarBackRepitched = ((uint64_t)howFarBack << 24) / (uint32_t)phaseIncrement + 1;
		if (repitchedBufferNumSamplesWritten >= howFarBackRepitched) {
			playHeads[PLAY_HEAD_NEWER].mode = PLAY_HEAD_MODE_REPITCHED_BUFFER;
			playHeads[PLAY_HEAD_NEWER].repitchedBufferReadPos =
			    (uint32_t)(repitchedBufferWritePos - howFarBackRepitched) & (INPUT_REPITCHED_BUFFER_SIZE - 1);
			goto thatsDone;
		}
	}
#endif
	// If still here, use raw direct buffer - usually because we're pitching down

	if (phaseIncrement == 16777216) {
		playHeads[PLAY_HEAD_NEWER].mode = PLAY_HEAD_MODE_RAW_DIRECT;
		playHeads[PLAY_HEAD_NEWER].rawBufferReadPos = numRawSamplesProcessedAtNowTime & (INPUT_RAW_BUFFER_SIZE - 1);
		Debug::println("raw hop");
	}

	else {
		playHeads[PLAY_HEAD_NEWER].mode = PLAY_HEAD_MODE_RAW_REPITCHING;

		//memset(playHeads[PLAY_HEAD_NEWER].interpolationBuffer, 0, sizeof(playHeads[PLAY_HEAD_NEWER].interpolationBuffer));
		playHeads[PLAY_HEAD_NEWER].fillInterpolationBuffer(liveInputBuffer, numChannels);
		playHeads[PLAY_HEAD_NEWER].oscPos = additionalOscPos;

		//Debug::print("playing from: ");
		//Debug::println(playHeads[PLAY_HEAD_NEWER].rawBufferReadPos);
	}

thatsDone:

	playHeads[PLAY_HEAD_NEWER].percPos = 0xFFFFFFFF;

	if (thisCrossfadeLength) {
		crossfadeProgress = 0;
		crossfadeIncrement = (16777216 - 1) / thisCrossfadeLength + 1;
	}
	else {
		crossfadeProgress = 16777216;
	}

	//Debug::print("crossfade length: ");
	//Debug::println(thisCrossfadeLength);

	/*
	if (phaseIncrement > 16777216) {
		uint64_t totalPlayableSamples = ((uint64_t)howFarBack << 24) / (uint32_t)(phaseIncrement - 16777216);
		totalPlayableSamples = getMax(totalPlayableSamples, (uint64_t)2);

		totalPlayableSamples = getMin(totalPlayableSamples, (uint64_t)16384);

		if (nextCrossfadeLength > totalPlayableSamples - 1) nextCrossfadeLength = totalPlayableSamples - 1;

		samplesTilHopEnd = totalPlayableSamples - nextCrossfadeLength;
	}
	*/

	considerRepitchedBuffer(phaseIncrement);

#if INPUT_ENABLE_REPITCHED_BUFFER
	if (repitchedBuffer && !stillWritingToRepitchedBuffer
	    && playHeads[PLAY_HEAD_NEWER].mode != PLAY_HEAD_MODE_REPITCHED_BUFFER
	    && playHeads[PLAY_HEAD_OLDER].mode != PLAY_HEAD_MODE_REPITCHED_BUFFER) {
		generalMemoryAllocator.dealloc(repitchedBuffer);
		repitchedBuffer = NULL;
	}
#endif
	samplesIntoHop = 0;

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t timeTaken = endTime - startTime;
	Debug::print("hop end time: ");
	Debug::println(timeTaken);
#endif
}

void LivePitchShifter::considerRepitchedBuffer(int32_t phaseIncrement) {
#if INPUT_ENABLE_REPITCHED_BUFFER
	// Consider whether to have the repitchedBuffer
	if (phaseIncrement > 16777216) {
		if (!repitchedBuffer) {

			repitchedBuffer = (int32_t*)generalMemoryAllocator.alloc(
			    INPUT_REPITCHED_BUFFER_SIZE * sizeof(int32_t) * numChannels, NULL, false, true);
			if (repitchedBuffer) {
				repitchedBufferWritePos = 0;
				oscPos = 0;
				repitchedBufferNumSamplesWritten = 0;
				stillWritingToRepitchedBuffer = true;
				memset(interpolationBuffer, 0, sizeof(interpolationBuffer));
			}
		}
	}

	else {
		if (repitchedBuffer) {
			stillWritingToRepitchedBuffer = false;
		}
	}
#endif
}

bool LivePitchShifter::olderPlayHeadIsCurrentlySounding() {
	return (crossfadeProgress < 16777216);
}

bool LivePitchShifter::mayBeRemovedWithoutClick() {
	return (!olderPlayHeadIsCurrentlySounding() && playHeads[PLAY_HEAD_NEWER].mode == PLAY_HEAD_MODE_RAW_DIRECT);
}

#if INPUT_ENABLE_REPITCHED_BUFFER
// TODO: this is identical to in SampleLowLevelReader - combine
void LivePitchShifter::interpolate(int32_t* sampleRead, int interpolationBufferSize, int numChannelsNow,
                                   int whichKernel) {
#include "dsp/interpolation/interpolate.h"
}
#endif
