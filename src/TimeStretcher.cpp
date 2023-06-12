/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <Cluster.h>
#include "TimeStretcher.h"
#include "definitions.h"
#include <string.h>
#include "Sample.h"
#include "VoiceSample.h"
#include "GeneralMemoryAllocator.h"
#include "storagemanager.h"
#include "SampleCache.h"
//#include <math.h>
#include <stdlib.h>
#include "functions.h"
#include "SampleHolder.h"
#include "SamplePlaybackGuide.h"
#include "playbackhandler.h"
#include "uart.h"

#define MEASURE_HOP_END_PERFORMANCE 0

bool TimeStretcher::init(Sample* sample, VoiceSample* voiceSample, SamplePlaybackGuide* guide, int64_t newSamplePosBig,
                         int numChannels, int32_t phaseIncrement, int32_t timeStretchRatio, int playDirection,
                         int priorityRating, int fudgingNumSamplesTilLoop, int loopingType) {

	AudioEngine::logAction("TimeStretcher::init");

	//Uart::println("TimeStretcher::init");

	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		clustersForPercLookahead[l] = NULL;
	}

	for (int l = 0; l < 2; l++) {
		percCacheClustersNearby[l] = NULL;
	}

	playHeadStillActive[PLAY_HEAD_OLDER] = true;
	playHeadStillActive[PLAY_HEAD_NEWER] = true;

	samplePosBig = newSamplePosBig;

	buffer = NULL;

	numTimesMissedHop = 0;

#if TIME_STRETCH_ENABLE_BUFFER
	bufferFillingMode = BUFFER_FILLING_OFF;

	if (phaseIncrement)
		reassessWhetherToBeFillingBuffer(phaseIncrement, timeStretchRatio, BUFFER_FILLING_NEWER, numChannels);

	newerHeadReadingFromBuffer = false;

	// If that did set up buffering, from the newer play-head, then just set up the "older" play-head to read from the buffer
	if (bufferFillingMode == BUFFER_FILLING_NEWER) {
		olderHeadReadingFromBuffer = true;
		olderBufferReadPos = 0;
	}

	// Otherwise...
	else
#endif
	{
		olderPartReader.cloneFrom(voiceSample, fudgingNumSamplesTilLoop); // Steals reasons if fudging
		olderHeadReadingFromBuffer = false;
	}

	// Rare case of fudging
	if (fudgingNumSamplesTilLoop) {

		int numSamplesIntoPreMarginToStartSource = fudgingNumSamplesTilLoop;

		if (phaseIncrement != 16777216) {
			fudgingNumSamplesTilLoop =
			    ((uint64_t)fudgingNumSamplesTilLoop * (uint32_t)phaseIncrement + (1 << 23)) >> 24; // Round
		}

		int bytesPerSample = sample->byteDepth * sample->numChannels;

		int newBytePos =
		    guide->getBytePosToStartPlayback(true) - fudgingNumSamplesTilLoop * bytesPerSample * playDirection;

		int32_t startByte = sample->audioDataStartPosBytes;
		if (playDirection != 1)
			startByte +=
			    sample->audioDataLengthBytes
			    - bytesPerSample; // The actual first sample of the waveform in our given direction, regardless of our elected start-point

		// If there's actually some waveform where we propose to start, do it!
		if ((newBytePos - startByte) * playDirection >= 0) {}
		else return false; // Shouldn't happen

		bool success = setupNewPlayHead(sample, voiceSample, guide, newBytePos, 0, priorityRating, loopingType);
		if (!success) return false;

		samplesTilHopEnd = 2147483647; // We don't want to do a hop-end

		crossfadeIncrement =
		    (uint32_t)(16777216 + fudgingNumSamplesTilLoop) / (uint32_t)fudgingNumSamplesTilLoop; // Round up
		crossfadeProgress = 0;
	}

	// Normal case
	else {

		olderPartReader.interpolationBufferSizeLastTime = 0;

		// The fine-tuning of the first hop length is important for allowing individual drum hits to sound shorter when sped up.
		// We also add a slight random element so that if many AudioClips or other Sounds begin and do time-stretching at the same time,
		// they won't all hit the CPU with their first hop at the exact same time, which would cause a spike
		samplesTilHopEnd = TIME_STRETCH_DEFAULT_FIRST_HOP_LENGTH + ((int8_t)getRandom255() >> 2);

		crossfadeProgress = 16777216;
		crossfadeIncrement = 0;
	}

	AudioEngine::logAction("---/");

	return true;
}

void TimeStretcher::reInit(int64_t newSamplePosBig, SamplePlaybackGuide* guide, VoiceSample* voiceSample,
                           Sample* sample, int numChannels, int32_t timeStretchRatio, int32_t phaseIncrement,
                           uint64_t combinedIncrement, int playDirection, int loopingType, int priorityRating) {

	samplePosBig = newSamplePosBig;

	// Not quite sure if these two are necessary...
	//unassignAllReasonsForPercLookahead();
	//unassignAllReasonsForPercCacheClusters();

	// If the newer play-head is still active, we'll have a hop soon, so playback will soon we coming from the start of the waveform again like we want, and
	// we don't have to do anything.
	//
	// Or if the newer play-head is inactive, force a hop now. Is this the best way? Not sure if maybe we wanna instead just do an init() or at least force the new
	// play-head to exactly the start-pos-sample or something?
	if (!playHeadStillActive[PLAY_HEAD_NEWER]) {
		hopEnd(guide, voiceSample, sample, numChannels, timeStretchRatio, phaseIncrement, combinedIncrement,
		       playDirection, loopingType, priorityRating);
	}
}

void TimeStretcher::beenUnassigned() {
	unassignAllReasonsForPercLookahead();
	unassignAllReasonsForPercCacheClusters();
	olderPartReader.unassignAllReasons();
	if (buffer) generalMemoryAllocator.dealloc(buffer);
}

void TimeStretcher::unassignAllReasonsForPercLookahead() {
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		if (clustersForPercLookahead[l]) {
			audioFileManager.removeReasonFromCluster(clustersForPercLookahead[l], "E130");
			clustersForPercLookahead[l] = NULL;
		}
	}
}

void TimeStretcher::unassignAllReasonsForPercCacheClusters() {
	for (int l = 0; l < 2; l++) {
		if (percCacheClustersNearby[l]) {
			audioFileManager.removeReasonFromCluster(percCacheClustersNearby[l], "E132");
			percCacheClustersNearby[l] = NULL;
		}
	}
}

const int16_t minHopSizeCoarse[] = {2500, 3000, 3000, 600, 300};
const int16_t minHopSizeFine[] = {
    3000, 3000, 3000, 3000, 3000, 3000, 3000, 3000, // -12, ....
    3000, 2500, 2000, 1500, 1000, 900,  800,  700,  // +0, ....
    600                                             // +12
};

const int16_t maxHopSizeCoarse[] = {5000, 6500, 11000, 4000, 2500};
const int16_t maxHopSizeFine[] = {
    6500,  7000, 8000, 9000, 9500, 9750, 10000, 11000, // -12, ....
    11000, 7500, 8000, 6500, 5000, 4750, 4500,  4250,  // +0, ....
    4000                                               // +12
};

const int16_t crossfadeProportionalCoarse[] = {200, 160, 0, 9, 9};
const int16_t crossfadeProportionalFine[] = {
    160, 140, 125, 110, 90, 70, 50, 20, // -12, ....
    0,   20,  20,  20,  20, 17, 14, 11, // +0, ....
    9                                   // +12
};

const int16_t crossfadeAbsoluteCoarse[] = {10, 10, 60, 40, 20};
const int16_t crossfadeAbsoluteFine[] = {
    10, 10, 10, 10, 10, 10, 10, 170, // -12, ....
    60, 90, 20, 30, 40, 40, 40, 40,  // +0, ....
    40                               // +12
};

const int16_t randomCoarse[] = {85, 120, 0, 0, 0};
const int16_t randomFine[] = {
    120, 95, 70, 45, 20, 15, 10, 10, // -12, ....
    0,   0,  0,  0,  0,  0,  0,  0,  // +0, ....
    0                                // +12
};

/*
const int16_t lookaheadCoarse[] = {1100, 1000, 1000, 0, 0};
const int16_t lookaheadFine[] = {
		1000, 1000, 1000, 1000, 1000, 850, 700, 700, // -12, ....
		1000, 1200, 0, 0, 0, 0, 0, 0, // +0, ....
		0 // +12
};
*/

// Returns false if sound needs to cut due to a load error or similar
bool TimeStretcher::hopEnd(SamplePlaybackGuide* guide, VoiceSample* voiceSample, Sample* sample, int numChannels,
                           int32_t timeStretchRatio, int32_t phaseIncrement, uint64_t combinedIncrement,
                           int playDirection, int loopingType, int priorityRating) {

	AudioEngine::logAction("hopEnd");

#if ALPHA_OR_BETA_VERSION
	// Trying to track down Steven's E133 - percCacheClusterNearby pointing to things with no reasons left
	for (int l = 0; l < 2; l++) {
		if (percCacheClustersNearby[l] && !percCacheClustersNearby[l]->numReasonsToBeLoaded) {
			numericDriver.freezeWithError("i036");
		}
	}
#endif

	/*
	if (numTimesMissedHop) {
		Uart::print("missed ");
		Uart::println(numTimesMissedHop);
	}
	*/

	AudioEngine::numHopsEndedThisRoutineCall++;

	numTimesMissedHop = 0;

	//AudioEngine::bypassCulling = true; // This is kinda dangerous, but I think it should kinda be ok...

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t startTime = MTU2.TCNT_0;
#endif

	int reversed = (playDirection == 1) ? 0 : 1;

	int byteDepth = sample->byteDepth;
	int bytesPerSample = byteDepth * numChannels;

	int32_t oldHeadBytePos;

	//Uart::println("");
	//Uart::println("hopEnd ------");

	olderHeadReadingFromBuffer = false;
	oldHeadBytePos = voiceSample->getPlayByteLowLevel(sample, guide, true);
	olderPartReader.cloneFrom(voiceSample, true); // Steals all reasons from the VoiceSample
	playHeadStillActive[PLAY_HEAD_OLDER] = playHeadStillActive[PLAY_HEAD_NEWER];
	playHeadStillActive[PLAY_HEAD_NEWER] = true;
	hasLoopedBackIntoPreMargin = false; // Might get set to true below

	int maxHopLength = 2147483647;

	int64_t samplePos;

	// If guide is synced to the actual sequence's ticks, we can peeeerfectly get the pos we want.
	// Note: slight imperfection but it should be fine - if an AudioClip has had its position "resumed" to somewhere else due to a <>+play, it could still have its
	// envelope fading out, yet now also be doingLateStart, meaning we're still rendering its old position, but now this new hop is going to start from
	// the new, resume()d current play-pos. Should be fine and if anything should sound not bad... but maybe beware? Should we refuse to do a new hop, or to make this call
	// to guide->getSyncedNumSamplesIn(), if an AudioClip is fading/releasing out?
	if (guide->sequenceSyncLengthTicks && playbackHandler.isEitherClockActive()) {
		uint64_t numSamplesIn = guide->getSyncedNumSamplesIn();

		uint64_t startSample = (uint32_t)(guide->startPlaybackAtByte - sample->audioDataStartPosBytes)
		                       / (uint8_t)(sample->numChannels * sample->byteDepth);

		samplePos = startSample + numSamplesIn * playDirection;
		// There should be no risk of that passing the end of the sample zone I think...

		samplePosBig = (uint64_t)samplePos << 24;
	}

	// Otherwise, just look at our own sorta running record of the current pos
	else {
		samplePos = getSamplePos(playDirection);
	}

	int speedLog = quickLog(timeStretchRatio);

	int minBeamWidth; // Not pixellated
	int maxBeamWidth; // Not pixellated
	int crossfadeProportional;
	int crossfadeAbsolute;
	int randomElement;

	//int beamWidthIncrement = (uint32_t)phaseIncrement

	// Neutral is (832 << 20). Each octave is a (32 << 20)

	// If within +/- 1 octave...
	if (speedLog >= (800 << 20) && speedLog < (864 << 20)) {
		int position = speedLog - (800 << 20);

		minBeamWidth = interpolateTableSigned(position, 26, minHopSizeFine, 4) >> 16;
		maxBeamWidth = interpolateTableSigned(position, 26, maxHopSizeFine, 4) >> 16;
		crossfadeProportional = interpolateTableSigned(position, 26, crossfadeProportionalFine, 4) << 8;
		crossfadeAbsolute = interpolateTableSigned(position, 26, crossfadeAbsoluteFine, 4) >> 16;
		randomElement = interpolateTableSigned(position, 26, randomFine, 4);
		//lookahead = interpolateTableSigned(position, 26, lookaheadFine, 4) >> 16;
	}

	// Or if outside of that...
	else {
		if (speedLog > (896 << 20)) speedLog = (896 << 20);
		else if (speedLog < (768 << 20)) speedLog = (768 << 20);

		int position = speedLog - (768 << 20);

		minBeamWidth = interpolateTableSigned(position, 27, minHopSizeCoarse, 2) >> 16;
		maxBeamWidth = interpolateTableSigned(position, 27, maxHopSizeCoarse, 2) >> 16;
		crossfadeProportional = interpolateTableSigned(position, 27, crossfadeProportionalCoarse, 2) << 8;
		crossfadeAbsolute = interpolateTableSigned(position, 27, crossfadeAbsoluteCoarse, 2) >> 16;
		randomElement = interpolateTableSigned(position, 27, randomCoarse, 2);
		//lookahead = interpolateTableSigned(position, 27, lookaheadCoarse, 2) >> 16;
	}

	//Uart::print("maxBeamWidth: ");
	//Uart::println(maxBeamWidth);

	/*
	minBeamWidth = storageManager.devVarA * 10;
	maxBeamWidth = storageManager.devVarB * 100;
	crossfadeProportional = storageManager.devVarC << 24;
	crossfadeAbsolute = storageManager.devVarD;
	randomElement = storageManager.devVarF << 16;
*/

	// Apply random element
#if !MEASURE_HOP_END_PERFORMANCE
	minBeamWidth +=
	    (multiply_32x32_rshift32(minBeamWidth, multiply_32x32_rshift32(getNoise(), randomElement << 8)) << 2);
#endif

	int32_t newHeadBytePos;
	uint32_t crossfadeLengthSamples;

	int32_t additionalOscPos = 0;

	int32_t waveformStartByte = sample->audioDataStartPosBytes;
	if (playDirection != 1)
		waveformStartByte +=
		    sample->audioDataLengthBytes
		    - bytesPerSample; // The actual first sample of the waveform in our given direction, regardless of our elected start-point

	// If this is for some looping piece of audio (possibly an AudioClip, but also a looping instrument sample or something,
	// see if we want to place our next hop in the pre-margin.
	// Remember, for AudioClips which are not going to loop another time (determined by currentPlaybackMode->willClipLoopAtEnd()),
	// this will be set as false. That check is done in AudioClip::render()
	if (loopingType == LOOP_TIMESTRETCHER_LEVEL_IF_ACTIVE) {

		// First, check whether there's any pre-margin at all

		int numBytesOfPreMarginAvailable =
		    (int32_t)(guide->getBytePosToStartPlayback(true) - waveformStartByte) * playDirection;

		if (numBytesOfPreMarginAvailable > 0) {

			uint32_t loopEndSample =
			    (uint32_t)(guide->getBytePosToEndOrLoopPlayback()
			               - sample
			                     ->audioDataStartPosBytes) // This will refer to the loop point - not the actual end of the waveform
			    / (uint8_t)(sample->numChannels * sample->byteDepth);

			int sourceSamplesTilLoop = (int32_t)(loopEndSample - samplePos) * playDirection;

			if (sourceSamplesTilLoop >= 0) { // This can fail for an AudioClip if we've just stopped playback

				int outputSamplesTilLoop =
				    (((uint64_t)sourceSamplesTilLoop << 24) + (combinedIncrement >> 1)) / combinedIncrement; // Round

				// If we're right near the end and it's time to do a crossfade...
				if (outputSamplesTilLoop < ANTI_CLICK_CROSSFADE_LENGTH) {

					int numSamplesIntoPreMarginToStartSource = outputSamplesTilLoop;
					if (phaseIncrement != 16777216)
						numSamplesIntoPreMarginToStartSource =
						    (((uint64_t)sourceSamplesTilLoop << 24) + (timeStretchRatio >> 1))
						    / timeStretchRatio; // Round

					newHeadBytePos = guide->getBytePosToStartPlayback(true)
					                 - numSamplesIntoPreMarginToStartSource * bytesPerSample * playDirection;

					// If there's actually some waveform where we propose to start, do it!
					if ((int32_t)(newHeadBytePos - waveformStartByte) * playDirection >= 0) {

						// Note: we don't check that the relevant cluster has loaded. I think nothing too bad will happen if it's not...

						crossfadeLengthSamples = getMax(outputSamplesTilLoop, 10); // Min crossfade length

						samplesTilHopEnd =
						    minBeamWidth
						    >> 2; // Bigger sounds bad. Need to make smaller to match similarly resulting deduction which happens in the "normal" case
						samplesTilHopEnd = getMax(samplesTilHopEnd, crossfadeLengthSamples);

						crossfadeIncrement = (uint32_t)(16777215 + crossfadeLengthSamples)
						                     / (uint32_t)crossfadeLengthSamples; // Round up
						crossfadeProgress = 0;

						hasLoopedBackIntoPreMargin = true;

						//Uart::print("did special crossfade of length ");
						//Uart::println(crossfadeLengthSamples);

						// If there's a cache, we can't move a bit sideways to phase-align,
						// cos our new play-head needs to remain perfectly aligned with the start of the cache.
						if (voiceSample->cache) goto skipSearch;

						// Otherwise, if no cache, we totally do want to do that.
						else goto skipPercStuff;
					}
				}

				// Otherwise, just make sure we come back not long after the ideal time to do a crossfade back to before the start
				else {
					maxHopLength = outputSamplesTilLoop - ANTI_CLICK_CROSSFADE_LENGTH + 32;
				}
			}
		}
		else {
			//Uart::println("TimeStretcher sees there's no pre-margin");
		}
	}

	{
		// Apply repitching
		minBeamWidth = ((uint64_t)(uint32_t)minBeamWidth * phaseIncrement) >> 24;
		maxBeamWidth = ((uint64_t)(uint32_t)maxBeamWidth * phaseIncrement) >> 24;

		int bestBeamWidth = (minBeamWidth + maxBeamWidth) >> 1;

		int32_t beamPosAtTop = samplePos >> PERC_BUFFER_REDUCTION_MAGNITUDE; // Pixellated

		int earliestPixellatedPos, latestPixellatedPos;
		uint8_t* percCache =
		    sample->prepareToReadPercCache(beamPosAtTop, playDirection, &earliestPixellatedPos, &latestPixellatedPos);

		if (percCache) {

			int furthestBackSearched = beamPosAtTop;
			int furthestForwardSearched = beamPosAtTop;

			int totalPercussiveness = 0;
			int bestTotal = 0;
			int bestPixellatedBeamWidth = 1;

			for (uint32_t beamWidthNow = minBeamWidth; beamWidthNow < maxBeamWidth;
			     beamWidthNow += PERC_BUFFER_REDUCTION_SIZE) {

				int beamBackEdge = beamPosAtTop
				                   + (int32_t)(((int64_t)beamWidthNow * (timeStretchRatio - 16777216))
				                               >> (25 + PERC_BUFFER_REDUCTION_MAGNITUDE))
				                         * playDirection;
				int beamFrontEdge = beamPosAtTop
				                    + (int32_t)(((uint64_t)beamWidthNow * (timeStretchRatio + 16777216))
				                                >> (25 + PERC_BUFFER_REDUCTION_MAGNITUDE))
				                          * playDirection;

				int pixellatedBeamWidth = (beamFrontEdge - beamBackEdge) * playDirection;
				if (pixellatedBeamWidth) { // It might be zero near the start

					if ((beamFrontEdge - latestPixellatedPos) * playDirection > 0) {
						//Uart::println("hit front edge");
						break;
					}
					if ((beamBackEdge - earliestPixellatedPos) * playDirection < 0) {
						//Uart::println("hit back edge");
						break;
					}

					while ((beamFrontEdge - furthestForwardSearched) * playDirection > 0) {
						int percHere = percCache[furthestForwardSearched];
						totalPercussiveness += percHere;
						furthestForwardSearched += playDirection;
					}

					while ((beamBackEdge - furthestBackSearched) * playDirection > 0) {
						int percHere = percCache[furthestBackSearched];
						totalPercussiveness -= percHere;
						furthestBackSearched += playDirection;
					}

					while ((beamBackEdge - furthestBackSearched) * playDirection < 0) {
						furthestBackSearched -= playDirection; // Yes, do the decrement before reading in this one case
						int percHere = percCache[furthestBackSearched];
						totalPercussiveness += percHere;
					}

					// If our current average percussiveness is >= the previous best average (calculated without divisions)
					if (totalPercussiveness * bestPixellatedBeamWidth >= bestTotal * pixellatedBeamWidth) {
						bestTotal = totalPercussiveness;
						bestBeamWidth = beamWidthNow;
						bestPixellatedBeamWidth = pixellatedBeamWidth;
					}
				}
			}
		}

		int beamBackEdge = samplePos
		                   + (int32_t)(((int64_t)bestBeamWidth * (timeStretchRatio - 16777216)) >> 25)
		                         * playDirection; // The real, non-pixelated one

		int32_t waveformStartSample =
		    (playDirection == 1)
		        ? 0
		        : sample->lengthInSamples
		              - 1; // The actual first sample of the waveform in our given direction, regardless of our elected start-point
		int32_t waveformEndSample =
		    (playDirection == 1)
		        ? sample->lengthInSamples
		        : -1; // The actual last sample of the waveform in our given direction, regardless of our elected start-point

		// Still must make sure we didn't go back beyond the start of the waveform, which can end up happening from the heavily pixellated search thing above
		if ((int32_t)(beamBackEdge - waveformStartSample) * playDirection < 0) beamBackEdge = waveformStartSample;

		if (!olderPartReader.clusters[0]) Uart::println("No cluster!!!");

		samplesTilHopEnd =
		    ((uint64_t)bestBeamWidth << 24) / (uint32_t)phaseIncrement; // That's the beamWidthOnRepitchedWaveform
		if (samplesTilHopEnd < 1)
			samplesTilHopEnd = 1; // Can happen if pitch up very high and also time-stretch sped up a lot

		crossfadeLengthSamples =
		    multiply_32x32_rshift32_rounded(samplesTilHopEnd, crossfadeProportional) + crossfadeAbsolute * 4;
		if (crossfadeLengthSamples >= (samplesTilHopEnd >> 1)) crossfadeLengthSamples = (samplesTilHopEnd >> 1);

		samplesTilHopEnd -= crossfadeLengthSamples;

		// Apply maxHopLength
		samplesTilHopEnd = getMin(samplesTilHopEnd, maxHopLength);
		crossfadeLengthSamples = getMin(samplesTilHopEnd, crossfadeLengthSamples);

		crossfadeIncrement = (uint32_t)16777216 / (uint32_t)crossfadeLengthSamples;
		crossfadeProgress = 0;

		// Make sure we haven't shot past end of waveform. If so, we don't want this new play-head sounding
		if ((int32_t)(beamBackEdge - waveformEndSample) * playDirection >= 0) {
			playHeadStillActive[PLAY_HEAD_NEWER] = false;
			return true; // But don't cut the VoiceSample entirely
		}

		newHeadBytePos = sample->audioDataStartPosBytes + beamBackEdge * bytesPerSample;
	}

skipPercStuff:

	//AudioEngine::logAction("phase search init");

	// Search for minimum phase disruption on crossfade. Crucially, the exact instant in time we're going to be examining is not
	// the beginning play-point of the new play-head, but the point half-way through the crossfade later. Remember that!
	if (playHeadStillActive[PLAY_HEAD_OLDER]) { // Added condition, Aug 2019. Surely this makes sense...
		int lengthToAverageEach = ((uint64_t)phaseIncrement * TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH) >> 24;
		lengthToAverageEach = getMax(lengthToAverageEach, 1);
		lengthToAverageEach =
		    getMin(lengthToAverageEach, TIME_STRETCH_CROSSFADE_MOVING_AVERAGE_LENGTH * 2); // Keep things sensible

		int crossfadeLengthSamplesSource = ((uint64_t)crossfadeLengthSamples * phaseIncrement) >> 24;

		bool success;

		int32_t oldHeadTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];
		if (oldHeadBytePos < (int32_t)sample->audioDataStartPosBytes)
			goto skipSearch; // Would probably be possible on a pitch-adjusted reversed waveform if we'd got past the end and were "buffering zeros"
		success = sample->getAveragesForCrossfade(oldHeadTotals, oldHeadBytePos, crossfadeLengthSamplesSource,
		                                          playDirection, lengthToAverageEach);
		if (!success) goto skipSearch;

		int32_t newHeadTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];
		if (ALPHA_OR_BETA_VERSION && newHeadBytePos < (int32_t)sample->audioDataStartPosBytes)
			numericDriver.freezeWithError("E285");
		success = sample->getAveragesForCrossfade(newHeadTotals, newHeadBytePos, crossfadeLengthSamplesSource,
		                                          playDirection, lengthToAverageEach);
		if (!success) goto skipSearch;

		int32_t bestDifferenceAbs = getTotalDifferenceAbs(oldHeadTotals, newHeadTotals);
		int32_t bestOffset = 0;

		int32_t initialTotalChange = getTotalChange(oldHeadTotals, newHeadTotals);

		int searchDirection = playDirection;

		int readByte[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1];

		int samplePos = (uint32_t)(newHeadBytePos - sample->audioDataStartPosBytes) / (uint8_t)bytesPerSample;

		int samplePosMidCrossfade = samplePos + (crossfadeLengthSamplesSource >> 1) * playDirection;

		int readSample = samplePosMidCrossfade
		                 - ((lengthToAverageEach * TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES) >> 1) * playDirection;

		int firstReadByte = readSample * bytesPerSample + sample->audioDataStartPosBytes;

		int maxSearchSize = (samplesTilHopEnd * 40) >> 8; // I don't remember the logic behind this line...
		maxSearchSize = ((uint64_t)maxSearchSize * phaseIncrement) >> 24;

#if MEASURE_HOP_END_PERFORMANCE
		maxSearchSize = 441;
#endif

		int limit =
		    (sample->sampleRate / 45)
		    >> 1; // Allow tracking down to around 45Hz, at input. We >>1 again because this limit is just for searching in one direction, and we're going to do both directions.
		maxSearchSize = getMin(maxSearchSize, limit);
		//Uart::print("max search length: ");
		//Uart::println(maxSearchSize);

		int numFullDirectionsSearched = 0;
		int timesSignFlipped = 0;

		// Do the search. We'll come back again to search in the other direction too. Or we may come back sooner if we quickly decide that the other search direction would
		// be better

		if (false) {
restartSearchWithOtherDirection:
			searchDirection = -searchDirection;
		}

startSearch:
		//AudioEngine::logAction("startSearch:");

		int bytesPerSampleTimesSearchDirection = bytesPerSample * searchDirection;

		int32_t lastTotalChange = initialTotalChange;
		readByte[0] = firstReadByte;

		int searchDirectionRelativeToPlayDirection = searchDirection * playDirection;
		if (searchDirectionRelativeToPlayDirection == -1) {
			readByte[0] -= playDirection * bytesPerSample;
		}

		int32_t newHeadRunningTotals[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES];
		for (int i = 0; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES; i++) {
			newHeadRunningTotals[i] = newHeadTotals[i];
			readByte[i + 1] = readByte[i] + lengthToAverageEach * bytesPerSample * playDirection;
		}

		int offsetNow = 0;
		int numSamplesLeftThisSearch = maxSearchSize;

		do { // This will get restarted multiple times as we cross cluster boundaries and stuff

			// Ok, we're gonna read some samples...
			int numSamplesThisRead = numSamplesLeftThisSearch;

			char const* __restrict__ currentPos[TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1];

			// Setup the various points between the moving averages
			for (int i = 0; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1; i++) {

				int bytesTilWaveformEnd =
				    (searchDirection == 1)
				        ? (sample->audioDataStartPosBytes + sample->audioDataLengthBytes - readByte[i])
				        : readByte[i] - ((int32_t)sample->audioDataStartPosBytes - bytesPerSample);

				if (bytesTilWaveformEnd <= 0) goto searchNextDirection;

				int whichCluster = readByte[i] >> audioFileManager.clusterSizeMagnitude;
				Cluster* cluster = sample->clusters.getElement(whichCluster)->cluster;
				if (!cluster || !cluster->loaded) goto skipSearch;

				int bytePosWithinCluster = readByte[i] & (audioFileManager.clusterSize - 1);

				int bytesLeftThisCluster =
				    (searchDirection == -1)
				        ? (bytePosWithinCluster + bytesPerSample)
				        : (audioFileManager.clusterSize - bytePosWithinCluster + bytesPerSample - 1);

				int bytesWeMayRead = getMin(bytesTilWaveformEnd, bytesLeftThisCluster);

				int bytesWeWantToRead = numSamplesThisRead * bytesPerSample;
				if (bytesWeWantToRead > bytesWeMayRead) {
					numSamplesThisRead = (uint32_t)bytesWeMayRead / (uint8_t)bytesPerSample;
				}

				currentPos[i] = &cluster->data[bytePosWithinCluster] - 4 + byteDepth;
			}

			// Alright, read those samples for our currently worked out little bit until we reach a cluster boundary or something
			int endOffset = offsetNow + numSamplesThisRead * bytesPerSampleTimesSearchDirection;
			do { // For each individual sample (well, actually, we're gonna read like 3 different, spaced ones at a time)

				int32_t readValueRelativeToBothDirections;
				int32_t thisRunningTotal;
				int i = 0;

				goto entryPoint;

				// Grab this sample for each moving-average-boundary
				for (; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1; i++) { // Manually unrolling didn't speed up

					thisRunningTotal = newHeadRunningTotals[i - 1]
					                   - readValueRelativeToBothDirections; // Subtracts the one from last iteration
entryPoint:

					int32_t readValueHere = *(int32_t*)currentPos[i] >> 16;

					if (numChannels == 2) {
						readValueHere += *(int32_t*)(currentPos[i] + byteDepth) >> 16;
					}

					currentPos[i] += bytesPerSampleTimesSearchDirection;

					readValueRelativeToBothDirections = readValueHere * searchDirectionRelativeToPlayDirection;

					if (i > 0) {
						thisRunningTotal += readValueRelativeToBothDirections;
						newHeadRunningTotals[i - 1] = thisRunningTotal;
					}
				}

				int32_t differenceAbs = getTotalDifferenceAbs(oldHeadTotals, newHeadRunningTotals);

				// If our very first read is worse, let's switch search direction right now - that'll improve our odds
				if (offsetNow == 0 && searchDirectionRelativeToPlayDirection == 1 && !numFullDirectionsSearched
				    && differenceAbs > bestDifferenceAbs) {
					goto restartSearchWithOtherDirection;
				}

				offsetNow += bytesPerSampleTimesSearchDirection;

				// Keep track of best match
				bool thisOffsetIsBestMatch = (differenceAbs < bestDifferenceAbs);
				if (thisOffsetIsBestMatch) {
					bestDifferenceAbs = differenceAbs;
					bestOffset = offsetNow;
				}

				int32_t thisTotalChange = getTotalChange(oldHeadTotals, newHeadRunningTotals);

				// If sign just flipped...
				if (((uint32_t)thisTotalChange >> 31) != ((uint32_t)lastTotalChange >> 31)) {

					// Try going in between the samples for the most accurate positioning, lining-up-wise.
					// The benefit of this is visible on a spectrum analysis if you're pitching a high-pitched sine wave right down, while also time stretching it
					if (phaseIncrement != 16777216
					    && (thisOffsetIsBestMatch
					        || bestOffset
					               == offsetNow
					                      - bytesPerSampleTimesSearchDirection)) { // If best was this one or last one
						uint32_t thisTotalDifferenceAbs = std::abs(thisTotalChange);
						uint32_t lastTotalDifferenceAbs = std::abs(lastTotalChange);
						additionalOscPos = ((uint64_t)lastTotalDifferenceAbs << 24)
						                   / (uint32_t)(lastTotalDifferenceAbs + thisTotalDifferenceAbs);
						if (searchDirectionRelativeToPlayDirection == -1)
							additionalOscPos = 16777216 - additionalOscPos;
						if (thisOffsetIsBestMatch != (searchDirectionRelativeToPlayDirection == -1))
							bestOffset -= bytesPerSample * playDirection;
					}

					// After sign has flipped a certain number of times (in total, including both search directions), we can be fairly sure we've found a good fit
					// This needs to be 4. Any less, and we start getting lots of bad alignments - I did tests. But 4 sounds basically as good as no limit.
					timesSignFlipped++;
#if !MEASURE_HOP_END_PERFORMANCE
					if (timesSignFlipped >= 4) goto stopSearch;
#endif
				}

				lastTotalChange = thisTotalChange;
			} while (offsetNow != endOffset);

			numSamplesLeftThisSearch -= numSamplesThisRead;

			for (int i = 0; i < TIME_STRETCH_CROSSFADE_NUM_MOVING_AVERAGES + 1; i++) {
				readByte[i] += bytesPerSampleTimesSearchDirection * numSamplesThisRead;
			}

		} while (numSamplesLeftThisSearch);

searchNextDirection:
		// Search the other direction if we haven't already
		numFullDirectionsSearched++;
		if (numFullDirectionsSearched < 2) goto restartSearchWithOtherDirection;

stopSearch:

		if (phaseIncrement != 16777216) {
			additionalOscPos += olderPartReader.oscPos;
			if (additionalOscPos >= 16777216) {
				additionalOscPos -= 16777216;
				bestOffset += bytesPerSample * playDirection;
			}
		}

		newHeadBytePos += bestOffset;

		// The above is supposed to not go back beyond the start of the waveform, but there must be some bug because it does. Until I fix that,
		// this check ensures we stay within the waveform
		if ((newHeadBytePos - waveformStartByte) * playDirection < 0) {
			Uart::println("avoided going before 0");
			Uart::println(newHeadBytePos - waveformStartByte);
			newHeadBytePos = waveformStartByte;
		}
	}

skipSearch:

#if TIME_STRETCH_ENABLE_BUFFER
	// If we might want to set up reading from buffer...
	if (bufferFillingMode != BUFFER_FILLING_OFF // If not OFF, it can only be OLDER or NEITHER - it gets changed above
	    && phaseIncrement != 16777216) {

		if (!olderPartReader.clusters[0]) {
			Uart::println("aaa");
		}

		int32_t bytesBehind = (olderPartReader.getPlayByteLowLevel(sample, guide) - newHeadBytePos) * playDirection;

		//Uart::print("bytesBehind: ");
		//Uart::println(bytesBehind);

		// Only proceed if newer head is earlier than older one
		if (bytesBehind < 0) goto optForDirectReading;

		uint32_t samplesBehind = (uint32_t)bytesBehind / (uint8_t)(sample->numChannels * sample->byteDepth);
		int samplesBehindOnRepitchedWaveform = ((uint64_t)samplesBehind << 24) / (uint32_t)phaseIncrement;
		int maxSamplesBehind = TIME_STRETCH_BUFFER_SIZE - (SSI_TX_BUFFER_NUM_SAMPLES - 1);

		// Check we're not earlier by too much - which would usually be because we just looped
		if (samplesBehindOnRepitchedWaveform > maxSamplesBehind) goto optForDirectReading;

		if (bufferSamplesWritten < samplesBehindOnRepitchedWaveform) {

			Uart::println("nope");

			Uart::print("samplesBehindOnRepitchedWaveform: ");
			Uart::println(samplesBehindOnRepitchedWaveform);
			Uart::print("bufferSamplesWritten: ");
			Uart::println(bufferSamplesWritten);
			goto optForDirectReading;
		}

		//Uart::print("samplesBehindOnRepitchedWaveform: ");
		//Uart::println(samplesBehindOnRepitchedWaveform);

		if (!samplesBehindOnRepitchedWaveform) {
			//Uart::println("new head reading non-buffered and writing to buffer");
			voiceSample->cloneFrom(&olderPartReader, false);
			newerHeadReadingFromBuffer = false;
			olderHeadReadingFromBuffer = true;
			olderBufferReadPos = bufferWritePos; // TODO: is this right?
			if (olderBufferReadPos != bufferWritePos)
				Uart::println("aaaaaaaaaaaa!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
			bufferFillingMode = BUFFER_FILLING_NEWER;
		}
		else {
			//Uart::println("new head reading buffered");
			newerBufferReadPos =
			    (uint32_t)(bufferWritePos - samplesBehindOnRepitchedWaveform) & (TIME_STRETCH_BUFFER_SIZE - 1);
			newerHeadReadingFromBuffer = true;
			//Uart::print("samples behind: ");
			//Uart::println(samplesBehindOnRepitchedWaveform);
		}
	}

	// Or, set up reading directly from audio file Clusters
	else {
optForDirectReading:
		//Uart::println("head reading non-buffered");
		newerHeadReadingFromBuffer = false;
#endif

		bool success =
		    setupNewPlayHead(sample, voiceSample, guide, newHeadBytePos, additionalOscPos, priorityRating, loopingType);
		if (!success) {

			Uart::println("setupNewPlayHead failed. Sticking with old");

			voiceSample->cloneFrom(&olderPartReader, true); // Steals all reasons back
			playHeadStillActive[PLAY_HEAD_NEWER] = playHeadStillActive[PLAY_HEAD_OLDER];
			playHeadStillActive[PLAY_HEAD_OLDER] = false;

			crossfadeIncrement = 0;
			samplesTilHopEnd = 500; // Come back in a while and try again
		}

#if TIME_STRETCH_ENABLE_BUFFER
		// We've stopped writing to the buffer for now, but it might still be being read from
		if (bufferFillingMode == BUFFER_FILLING_OLDER) bufferFillingMode = BUFFER_FILLING_NEITHER;
	}

	int newBufferFillingMode = BUFFER_FILLING_NEWER; // Letting it fill from the older buffer initially caused problems
	reassessWhetherToBeFillingBuffer(phaseIncrement, timeStretchRatio, newBufferFillingMode, numChannels);

#else
	// If no one's reading from the buffer anymore, stop filling it
	if (buffer
	    && !olderHeadReadingFromBuffer) { // olderHeadReadingFromBuffer will always be false - we set it above, at the start
		generalMemoryAllocator.dealloc(buffer);
		buffer = NULL;
		Uart::println("abandoning buffer!!!!!!!!!!!!!!!!");
	}

#endif

#if MEASURE_HOP_END_PERFORMANCE
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t timeTaken = endTime - startTime;
	Uart::print("hop end time: ");
	Uart::println(timeTaken);
#endif

	AudioEngine::logAction("/hopEnd");

	return true;
}

bool TimeStretcher::setupNewPlayHead(Sample* sample, VoiceSample* voiceSample, SamplePlaybackGuide* guide,
                                     int newHeadBytePos, int additionalOscPos, int priorityRating, int loopingType) {
	bool success = voiceSample->setupClustersForPlayFromByte(guide, sample, newHeadBytePos, priorityRating);
	if (!success) return false;

	success = voiceSample->changeClusterIfNecessary(
	    guide, sample, (loopingType == LOOP_LOW_LEVEL),
	    priorityRating); // Set looping as false - change in June 2019. Pretty sure this is right...
	if (!success) return false;

	voiceSample->interpolationBufferSizeLastTime = 0;
	voiceSample->oscPos = additionalOscPos;
	if (!voiceSample->clusters[0]) {
		playHeadStillActive[PLAY_HEAD_NEWER] = false;
		Uart::println("new no longer active");
	}

	return true;
}

#if TIME_STRETCH_ENABLE_BUFFER
void TimeStretcher::reassessWhetherToBeFillingBuffer(int32_t phaseIncrement, int32_t timeStretchRatio,
                                                     int newBufferFillingMode, int numChannels) {

	// If not currently filling buffer...
	if (bufferFillingMode == BUFFER_FILLING_OFF) {

		// If need to start filling buffer...
		if (phaseIncrement != 16777216 && timeStretchRatio < 16777216) {

			bool success = allocateBuffer(numChannels);
			if (success) {
				bufferFillingMode = newBufferFillingMode;

				bufferWritePos = 0;
				bufferSamplesWritten = 0;
				Uart::println("setting up buffer !!!!!!!!!!!!!!!!");
				if (bufferFillingMode == BUFFER_FILLING_OLDER) Uart::println(" - filling older");
				else Uart::println(" - filling newer");
			}
		}
	}

	// Or if we have been filling buffer...
	else {

		// If no one's reading from the buffer anymore, stop filling it
		if (!newerHeadReadingFromBuffer && !olderHeadReadingFromBuffer && bufferFillingMode == BUFFER_FILLING_NEITHER) {
			bufferFillingMode = BUFFER_FILLING_OFF;
			generalMemoryAllocator.dealloc(buffer);
			buffer = NULL;
			Uart::println("abandoning buffer!!!!!!!!!!!!!!!!");
		}
	}
}
#endif

bool TimeStretcher::allocateBuffer(int numChannels) {
	buffer = (int32_t*)generalMemoryAllocator.alloc(TIME_STRETCH_BUFFER_SIZE * sizeof(int32_t) * numChannels, NULL,
	                                                false, true);
	return (buffer != NULL);
}

void TimeStretcher::readFromBuffer(int32_t* __restrict__ oscBufferPos, int numSamples, int numChannels,
                                   int numChannelsAfterCondensing, int32_t sourceAmplitudeNow,
                                   int32_t amplitudeIncrementNow, int* __restrict__ bufferReadPos) {

	int32_t* oscBufferEndNow = oscBufferPos + numSamples * numChannels;

	do {
		int32_t sampleRead[2];

		if (numChannels == 2) {
			sampleRead[0] = buffer[*bufferReadPos * 2];
			sampleRead[1] = buffer[*bufferReadPos * 2 + 1];
		}
		else {
			sampleRead[0] = buffer[*bufferReadPos];
		}

		*bufferReadPos = (*bufferReadPos + 1) & (TIME_STRETCH_BUFFER_SIZE - 1);

		// If condensing to mono, do that now
		if (numChannels == 2 && numChannelsAfterCondensing == 1) {
			sampleRead[0] = ((sampleRead[0] >> 1) + (sampleRead[1] >> 1));
		}

		sourceAmplitudeNow += amplitudeIncrementNow;

		// Mono / left channel (or stereo condensed to mono)
		*oscBufferPos +=
		    multiply_32x32_rshift32(sampleRead[0], sourceAmplitudeNow); // sourceAmplitude is modified above
		oscBufferPos++;

		// Right channel
		if (numChannelsAfterCondensing == 2) {
			*oscBufferPos += multiply_32x32_rshift32(sampleRead[1], sourceAmplitudeNow);
			oscBufferPos++;
		}
	} while (oscBufferPos != oscBufferEndNow);
}

// Adds reason if this one wasn't already remembered here.
// And just to be super clear, this is for remembering links to *PERC CACHE Clusters*! Not just regular audio data Clusters.
void TimeStretcher::rememberPercCacheCluster(Cluster* cluster) {

	if (percCacheClustersNearby[0] == cluster || percCacheClustersNearby[1] == cluster) {
		return;
	}

	audioFileManager.addReasonToCluster(cluster);

	if (percCacheClustersNearby[0]) {
		audioFileManager.removeReasonFromCluster(percCacheClustersNearby[0],
		                                         "E133"); // Steven G got this on V3.1.5, Feb 2021!
	}
	percCacheClustersNearby[0] = percCacheClustersNearby[1];

	percCacheClustersNearby[1] = cluster;
}

// This is just responsible for adding reasons to the upcoming Clusters of sample audio data that the perc lookahead is going to need in the next little while, to reserve it and hopefully make sure it's loaded and in memory when we need it.
void TimeStretcher::updateClustersForPercLookahead(Sample* sample, uint32_t sourceBytePos, int playDirection) {
	int clusterIndex = sourceBytePos >> audioFileManager.clusterSizeMagnitude;

	if (!clustersForPercLookahead[0] || clustersForPercLookahead[0]->clusterIndex != clusterIndex) {
		unassignAllReasonsForPercLookahead();

		int nextClusterIndex = clusterIndex;
		for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
			if (nextClusterIndex < sample->getFirstClusterIndexWithAudioData()
			    || nextClusterIndex >= sample->getFirstClusterIndexWithNoAudioData())
				break; // If no more Clusters
			clustersForPercLookahead[l] =
			    sample->clusters.getElement(nextClusterIndex)->getCluster(sample, nextClusterIndex, CLUSTER_ENQUEUE);
			if (!clustersForPercLookahead[l]) break;
			nextClusterIndex += playDirection;
		}
	}
}

void TimeStretcher::setupCrossfadeFromCache(SampleCache* cache, int cacheBytePos, int numChannels) {

	int numSamplesThisCacheRead = getMin(samplesTilHopEnd, (int32_t)TIME_STRETCH_BUFFER_SIZE - 1);

	// Ignore loop end point

	int32_t originalCacheWriteBytePos = cache->writeBytePos;

	// If we've reached the end of what's been written to the cache...
	int bytesTilCacheEnd = cache->writeBytePos - cacheBytePos;
	if (bytesTilCacheEnd <= CACHE_BYTE_DEPTH * numChannels) return;

	int cachedClusterIndex = cacheBytePos >> audioFileManager.clusterSizeMagnitude;
	int bytePosWithinCluster = cacheBytePos & (audioFileManager.clusterSize - 1);

	Cluster* cacheCluster = cache->getCluster(cachedClusterIndex);
	if (ALPHA_OR_BETA_VERSION && !cacheCluster) { // If it got stolen - but we should have already detected this above
		numericDriver.freezeWithError("E178");
	}
	int32_t* __restrict__ readPos = (int32_t*)&cacheCluster->data[bytePosWithinCluster - 4 + CACHE_BYTE_DEPTH];

	int bytesTilCacheClusterEnd =
	    audioFileManager.clusterSize - bytePosWithinCluster
	    + (CACHE_BYTE_DEPTH * numChannels - 1); // Add one-byte-less-than-a-sample to it, so it'll round up
	if (bytesTilCacheClusterEnd <= CACHE_BYTE_DEPTH * numChannels) return; // TODO: allow it go to the next Cluster

	if (!buffer) {
		bool success = allocateBuffer(numChannels);
		if (!success) return;
	}

	// If we're really unlucky, allocating the buffer may have stolen from the cache
	if (originalCacheWriteBytePos != cache->writeBytePos) {
		generalMemoryAllocator.dealloc(buffer);
		buffer = NULL;
		return;
	}

	int bytesTilThisWindowEnd = getMin(bytesTilCacheClusterEnd, bytesTilCacheEnd);

#if CACHE_BYTE_DEPTH == 3
	int samplesTilThisWindowEnd =
	    (uint32_t)bytesTilThisWindowEnd
	    / (uint8_t)(numChannels * CACHE_BYTE_DEPTH); // Will round up, cos we did that additional bit above
#else
	int samplesTilThisWindowEnd = bytesTilThisWindowEnd >> CACHE_BYTE_DEPTH_MAGNITUDE;
	if (numChannels == 2) samplesTilThisWindowEnd >>= 1;
#endif

	if (samplesTilThisWindowEnd < numSamplesThisCacheRead)
		numSamplesThisCacheRead = samplesTilThisWindowEnd; // Only do this after we're sure we're not cancelling caching

	if (ALPHA_OR_BETA_VERSION && numSamplesThisCacheRead <= 0) numericDriver.freezeWithError("E179");

	for (int i = 0; i < numSamplesThisCacheRead; i++) {

		buffer[i * numChannels] = *readPos;

		readPos = (int32_t*)((char*)readPos + CACHE_BYTE_DEPTH);

		if (numChannels == 2) {
			buffer[i * 2 + 1] = *readPos;

			readPos = (int32_t*)((char*)readPos + CACHE_BYTE_DEPTH);
		}
	}

	olderHeadReadingFromBuffer = true;
	olderBufferReadPos = 0;
	crossfadeIncrement = 16777216 / (uint16_t)numSamplesThisCacheRead + 1;
	crossfadeProgress = 0;

	//Uart::print("doing crossfade from cache, length: ");
	//Uart::println(numSamplesThisCacheRead);

#if TIME_STRETCH_ENABLE_BUFFER
	bufferWritePos = TIME_STRETCH_BUFFER_SIZE - 1; // To trick it out of trying to do a "normal" thing later
	bufferFillingMode = BUFFER_FILLING_OFF;
#endif
}

int32_t TimeStretcher::getSamplePos(int playDirection) {
	if (playDirection == 1) return samplePosBig >> 24;
	else return (samplePosBig + 16777215) >> 24;
}

/*
int32_t a = getSine(((olderPlayPos & 63) << 20), 26) >> 11;
outputBuffer[1] = a * sample->percCacheMemory[(timeStretcher->distanceSinceStart >> PERC_BUFFER_REDUCTION_MAGNITUDE) >> 24];
//if (samplesTilHopEnd <= 3) outputBuffer[1] = 536870911;
if (hopStrength >= ((16777216 + (16777216 >> lShiftAmount)) >> 1) && !lastThingSilentNow) {
	lastThingSilentNow = true;
	outputBuffer[1] = -536870911;
}
*/
