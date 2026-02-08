/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "model/voice/voice_sample.h"
#include "definitions_cxx.hpp"
#include "dsp/timestretch/time_stretcher.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "model/voice/voice.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

extern "C" {}

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

void VoiceSample::beenUnassigned(bool wontBeUsedAgain) {
	unassignAllReasons(wontBeUsedAgain);
	endTimeStretching();
}

// You'll normally want to call setupClustersForInitialPlay() after this
void VoiceSample::noteOn(SamplePlaybackGuide* guide, uint32_t samplesLate, int32_t priorityRating) {

	doneFirstRenderYet = false;
	cache = nullptr;

	pendingSamplesLate = samplesLate; // We store this to deal with later, because in order to deal with this we need to
	                                  // know the pitch-adjustment, and that's not calculated yet
	oscPos = 0;
	interpolationBufferSizeLastTime = 0;
	timeStretcher = nullptr; // Just in case
	fudging = false;
	forAudioClip = false;
}

// Returns false if error
bool VoiceSample::noteOffWhenLoopEndPointExists(Voice* voice, VoiceSamplePlaybackGuide* guide) {

	if (cache) {
		cacheLoopEndPointBytes = 2147483647;
		return true;
	}
	else {
		if (timeStretcher) {
			return true;
		}
		else {
			return reassessReassessmentLocation(guide, (Sample*)guide->audioFileHolder->audioFile,
			                                    voice->getPriorityRating());
			// That's only going to make reassessmentLocation later, so no need to check we haven't shot past it I
			// think...
		}
	}
}

void VoiceSample::endTimeStretching() {
	fudging = false;

	if (timeStretcher) {
		timeStretcher->beenUnassigned();
		AudioEngine::timeStretcherUnassigned(timeStretcher);
		timeStretcher = nullptr;
	}
}

void VoiceSample::setupCacheLoopPoints(SamplePlaybackGuide* guide, Sample* sample, LoopType loopingType) {
	uint8_t bytesPerSample = sample->numChannels * sample->byteDepth;

	uint64_t combinedIncrement = ((uint64_t)(uint32_t)cache->phaseIncrement * (uint32_t)cache->timeStretchRatio) >> 24;

	bool endPointIsRightAtEnd =
	    (guide->playDirection == 1)
	        ? (guide->endPlaybackAtByte == sample->audioDataStartPosBytes + sample->audioDataLengthBytes)
	        : (guide->endPlaybackAtByte == sample->audioDataStartPosBytes - bytesPerSample);

	// End point: if it's right at the actual end of the sample, we can just fill the cache til it's full.
	if (endPointIsRightAtEnd) { // Easy
		cacheEndPointBytes = cache->waveformLengthBytes;
	}

	// Otherwise, have to calculate the exact byte at which we're ending
	else { // Hard
		uint32_t endPointBytes =
		    (guide->playDirection == 1)
		        ? guide->endPlaybackAtByte - sample->audioDataStartPosBytes
		        : sample->audioDataStartPosBytes + sample->audioDataLengthBytes - guide->endPlaybackAtByte - 1;
		uint32_t endPointSamples = endPointBytes / bytesPerSample - cache->skipSamplesAtStart;
		uint64_t endPointSamplesBig = (uint64_t)endPointSamples << 24;
		uint32_t endPointCombinedIncrements = (endPointSamplesBig - 1) / combinedIncrement + 1;

		cacheEndPointBytes = endPointCombinedIncrements * kCacheByteDepth * sample->numChannels;

		if (ALPHA_OR_BETA_VERSION && cacheEndPointBytes > cache->waveformLengthBytes) {
			D_PRINTLN("%d", cacheEndPointBytes);
			D_PRINTLN("%d", cache->waveformLengthBytes);
			FREEZE_WITH_ERROR("E128");
		}
	}

	// No looping
	if (loopingType == LoopType::NONE) {
		cacheLoopEndPointBytes = 2147483647;
	}

	// Yes looping
	else {
		// D_PRINTLN("yes looping");

		// Loop start point
		int32_t loopStartPointBytesRaw = guide->getLoopStartPlaybackAtByte();
		uint32_t loopStartPointBytes =
		    (guide->playDirection == 1)
		        ? loopStartPointBytesRaw - sample->audioDataStartPosBytes
		        : sample->audioDataStartPosBytes + sample->audioDataLengthBytes - loopStartPointBytesRaw - 1;
		int32_t loopStartPointSamples = loopStartPointBytes / bytesPerSample - cache->skipSamplesAtStart;

		// Loop end point
		int32_t loopEndPointBytesRaw = guide->getLoopEndPlaybackAtByte();

		int32_t loopEndPointBytes =
		    (guide->playDirection == 1)
		        ? loopEndPointBytesRaw - sample->audioDataStartPosBytes
		        : sample->audioDataStartPosBytes + sample->audioDataLengthBytes - loopEndPointBytesRaw - 1;
		int32_t loopEndPointSamples = loopEndPointBytes / bytesPerSample - cache->skipSamplesAtStart;

		// Loop length
		uint32_t loopLengthSamples = loopEndPointSamples - loopStartPointSamples;
		uint64_t loopLengthSamplesBig = (uint64_t)loopLengthSamples << 24;
		uint32_t loopLengthCombinedIncrements =
		    (loopLengthSamplesBig + (combinedIncrement >> 1)) / combinedIncrement; // Rounds
		cacheLoopLengthBytes = loopLengthCombinedIncrements * kCacheByteDepth * sample->numChannels;

		// Loop end point again
		uint64_t loopEndPointSamplesBig = (uint64_t)loopEndPointSamples << 24;
		uint32_t loopEndPointCombinedIncrements =
		    (loopEndPointSamplesBig + (combinedIncrement >> 1)) / combinedIncrement; // Rounds
		cacheLoopEndPointBytes = loopEndPointCombinedIncrements * kCacheByteDepth * sample->numChannels;
	}
}

// Returns a status such as LateStartAttemptStatus::WAIT
LateStartAttemptStatus VoiceSample::attemptLateSampleStart(SamplePlaybackGuide* voiceSource, Sample* sample,
                                                           int64_t rawSamplesSinceStart, int32_t numSamples) {

	int32_t bytesPerSample = sample->numChannels * sample->byteDepth;

	int64_t startAtByte =
	    rawSamplesSinceStart * bytesPerSample * voiceSource->playDirection + voiceSource->startPlaybackAtByte;

	// If we've already passed the end of the sample (how would that occur in the real world, again?)
	if ((int64_t)(startAtByte - voiceSource->endPlaybackAtByte) * voiceSource->playDirection >= 0) {
		return LateStartAttemptStatus::FAILURE;
	}

	if ((int64_t)(startAtByte - voiceSource->startPlaybackAtByte) * voiceSource->playDirection < 0) {
		FREEZE_WITH_ERROR("E439"); // Chasing "E366".
	}

	uint32_t startAtClusterIndex = startAtByte >> Cluster::size_magnitude;
	if (startAtClusterIndex >= sample->getFirstClusterIndexWithNoAudioData()) {
		// This can occur if some overflowing happened on the previous check due to an
		// insanely high rawSamplesSinceStart being supplied due to some other bug.
		// This was a problem around V3.1.0 release, so currently keeping this check
		// even outside of ALPHA_OR_BETA_VERSION.
		// Sven got! 4.0.0-beta4.
		FREEZE_WITH_ERROR("E366");
	}

	int32_t finalClusterIndex = voiceSource->getFinalClusterIndex(sample, cache); // Think this is right...

	int32_t clusterIndex = startAtClusterIndex;

	// We load our new Clusters into a secondary array first, to preserve the reason-holding power of whatever is
	// already in our main one until we unassign them below
	std::array<Cluster*, kNumClustersLoadedAhead> newClusters{};

	for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {

		// Grab it.
		newClusters[l] = sample->clusters.getElement(clusterIndex)->getCluster(sample, clusterIndex, CLUSTER_ENQUEUE);

		// If failure (would only happen in insanely rare case where there's no free RAM)
		if (l == 0 && !newClusters[l]) {
			return LateStartAttemptStatus::FAILURE;
		}

		// If that was the final Cluster, that's all we need to do
		if (clusterIndex == finalClusterIndex) {
			break;
		}

		clusterIndex += voiceSource->playDirection;
	}

	// Remove all old reasons - there might be some if this function has been called multiple times while we wait for
	// Clusters to load
	unassignAllReasons(false);

	// Copy in the new reasons we just made
	std::ranges::copy(newClusters, clusters.begin());

	// TODO: lots of this code is kinda tied to there being just two clusters looked-ahead (wait, not any more right?)

	// If the first Cluster has loaded...
	if (clusters[0]->loaded) {

		uint32_t bytesPosWithinCluster = startAtByte & (Cluster::size - 1);

		// If there's no second Cluster, or it's fully loaded... we're good to go!
		if (!clusters[1] || clusters[1]->loaded) {
goodToGo:
			setupForPlayPosMovedIntoNewCluster(voiceSource, sample, bytesPosWithinCluster, sample->byteDepth);

			pendingSamplesLate = 0;

			return LateStartAttemptStatus::SUCCESS;
		}

		// Or, if we're actually not very far into the first Cluster, that's fine too - the second one should still have
		// time to load
		else {
			int32_t numBytesIn;
			if (voiceSource->playDirection == 1) {
				numBytesIn = bytesPosWithinCluster;
			}
			else {
				numBytesIn = Cluster::size - bytesPosWithinCluster;
			}

			if (numBytesIn < (Cluster::size >> 1)) {
				goto goodToGo;
			}
		}
	}

	// If still here, that didn't work, so we have to wait, and come back later when hopefully some loading has taken
	// place
	pendingSamplesLate += numSamples;
	return LateStartAttemptStatus::WAIT;
}

// Returns false if becoming unassigned now
bool VoiceSample::fudgeTimeStretchingToAvoidClick(Sample* sample, SamplePlaybackGuide* guide, int32_t phaseIncrement,
                                                  int32_t numSamplesTilLoop, int32_t playDirection,
                                                  int32_t priorityRating) {

	D_PRINTLN("fudging  %d", numSamplesTilLoop);

	timeStretcher = AudioEngine::solicitTimeStretcher();
	if (!timeStretcher) {
		D_PRINTLN("fudging FAIL!!!!");
		return true; // That failed, but no need to unassign
	}

	int32_t playByte = getPlayByteLowLevel(sample, guide)
	                   - sample->audioDataStartPosBytes; // Allow for this to be negative. I'm not sure if it could in
	                                                     // this exact case of "fudging", but see the similar code below
	                                                     // in weShouldBeTimeStretchingNow() - better safe than sorry.
	int32_t playSample = divide_round_negative(playByte, sample->numChannels * sample->byteDepth);

	bool success =
	    timeStretcher->init(sample, this, guide, (int64_t)playSample << 24, sample->numChannels, phaseIncrement,
	                        kMaxSampleValue, playDirection, priorityRating, numSamplesTilLoop,
	                        LoopType::NONE); // Tell it no looping
	if (!success) {
		D_PRINTLN("fudging FAIL!!!!");
		return false; // It's too late to salvage anything - our play pos has probably been mucked around
	}

	success = reassessReassessmentLocation(
	    guide, sample,
	    priorityRating); // Got to - because time stretching affects the SampleLowLevelReader's adherence to markers
	// That's only going to make reassessmentLocation later, so no need to check we haven't shot past it I think...
	if (!success) {
		D_PRINTLN("fudging FAIL!!!!");
		return false;
	}

	fudging = true;

	return true;
}

// Returns false if becoming unassigned now
bool VoiceSample::weShouldBeTimeStretchingNow(Sample* sample, SamplePlaybackGuide* guide, int32_t numSamples,
                                              int32_t phaseIncrement, int32_t timeStretchRatio, int32_t playDirection,
                                              int32_t priorityRating, LoopType loopingType) {

	// If not set up yet, do it
	if (!timeStretcher) {
		timeStretcher = AudioEngine::solicitTimeStretcher();
		if (!timeStretcher) {
			return false;
		}

		int32_t playByte =
		    getPlayByteLowLevel(sample, guide)
		    - sample->audioDataStartPosBytes; // May return negative number - I think particularly if we're going in
		                                      // reversed and just cancelled reading from cache
		int32_t playSample = divide_round_negative(playByte, sample->numChannels * sample->byteDepth);

		timeStretcher->init(sample, this, guide, (int64_t)playSample << 24, sample->numChannels, phaseIncrement,
		                    timeStretchRatio, playDirection, priorityRating, 0, loopingType);
		bool success = reassessReassessmentLocation(
		    guide, sample,
		    priorityRating); // Got to - because time stretching affects the SampleLowLevelReader's adherence to markers
		// That's only going to make reassessmentLocation later, so no need to check we haven't shot past it I think...
		if (!success) {
			return false;
		}
	}

	// Read some perc cache
	int32_t playSample = timeStretcher->getSamplePos(playDirection);

	// Enforce a limit on how many samples can be rendered in one call to fillPercCache()
	// 32 is about minimum to avoid "hitting front edge" of perc cache when sped up double.
	// We'll do 32 if we're writing to main cache, cos that makes it extra important that we get the best sound.
	// Otherwise, go real easy on CPU
	int32_t maxNumSamplesToProcess = numSamples * ((cache && writingToCache) ? 32 : 6);

	if (timeStretchRatio != kMaxSampleValue) {
		sample->fillPercCache(timeStretcher, playSample, playSample + (phaseIncrement >> 10) * playDirection,
		                      playDirection, maxNumSamplesToProcess);
	}

	return true;
}

bool VoiceSample::stopReadingFromCache() {
	// Have to check Cluster is loaded, because we chose not to check this before, cos we didn't know if we'd actually
	// be reading from it
	if (!clusters[0] || !clusters[0]->loaded) {
		return false; // If it's not loaded we're screwed - do instant unassign
	}

	// .. we have to get reading to read normally, un-cached, so set some stuff up
	interpolationBufferSizeLastTime = 0;
	return true;
}

// Returns false if fail, which can happen if we've actually ended up past the finalClusterIndex cos we were reading
// cache before
bool VoiceSample::stopUsingCache(SamplePlaybackGuide* guide, Sample* sample, int32_t priorityRating,
                                 bool loopingAtLowLevel) {

	// If we were *writing* to the cache, nothing needs to change other than our discarding it. But if we were reading
	// from it...
	if (!writingToCache) {

		if (!stopReadingFromCache()) {
			return false;
		}
		// If time-stretching needs to be started back up, that'll happen below in call to considerTimeStretching()
	}

	cache = nullptr;

	// Now that cache is off, the SampleLowLevelReader probably needs to obey loop points (if no time stretching),
	// Although, as a side note, if we just abandoned reading cache, we might be just about to set time stretching up.
	// if (shouldObeyMarkers()) {	// Nope, we need to do this always now (fix Dec 2023), otherwise if a user does
	// something like change the tempo causing an AudioClip to change pitch
	// and abandon its cache, well it won't get its reassessmentLocation and clusterStartLocation set up if we don't do
	// this. Well, this only previously caused problems when shouldObeyMarkers() returned false - so only for
	// AudioClips, and only ones which weren't time-stretching (so had their pitch/speed set to LINKED and the audio was
	// being sped up or down like a record).

	// Now that we'll be reading the raw Sample source, we'll need to set up the reassessmentLocation and the
	// clusterStartLocation, so make this call. Also (I think) another reason for this call is that while writing cache,
	// low-level loop points aren't obeyed and we loop at the writing-to-cache level instead?
	bool stillGoing =
	    reassessReassessmentLocation(guide, sample,
	                                 priorityRating); // Returns false if fail, which can happen if we've actually ended
	                                                  // up past the finalClusterIndex cos we were reading cache before
	if (!stillGoing) {
		return false;
	}

	// This step added Sept 2020 after finding another similar bug which made me fairly sure this needs to be here, to
	// ensure currentPlayPos isn't past the new reassessmentLocation
	stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
	if (!stillGoing) {
		return false;
	}

	return true;
}

static_assert(TimeStretch::kDefaultFirstHopLength >= SSI_TX_BUFFER_NUM_SAMPLES,
              "problems with crossfading out of cache into new timeStretcher");

// Returning false means instant unassign
bool VoiceSample::render(SamplePlaybackGuide* guide, int32_t* __restrict__ outputBuffer, int32_t numSamples,
                         Sample* sample, int32_t sampleSourceNumChannels, LoopType loopingType, int32_t phaseIncrement,
                         int32_t timeStretchRatio, int32_t amplitude, int32_t amplitudeIncrement,
                         int32_t interpolationBufferSize, InterpolationMode desiredInterpolationMode,
                         int32_t priorityRating) {

	int32_t playDirection = guide->playDirection;

	// If there's a cache, check some stuff. Do this first, cos this can cause us to return
	// unlikely - if there is a cache this render will be fast, if not we definitely don't want to waste time looking
	if (cache) [[unlikely]] {

		// If relevant params have changed since before, we have to stop using the cache which those params previously
		// described
		if (phaseIncrement != cache->phaseIncrement || timeStretchRatio != cache->timeStretchRatio
		    || (phaseIncrement != kMaxSampleValue
		        && (desiredInterpolationMode != InterpolationMode::SMOOTH
		            || (interpolationBufferSize <= 2 && writingToCache)))) {

			bool needToAvoidClick = (!writingToCache && cache->timeStretchRatio != kMaxSampleValue);
			SampleCache* oldCache = cache;
			bool success = stopUsingCache(guide, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
			if (!success) {
				return false;
			}

			// Avoid click if cancelling reading time-stretched cache
			if (needToAvoidClick) {

				success = weShouldBeTimeStretchingNow(sample, guide, numSamples, phaseIncrement, timeStretchRatio,
				                                      playDirection, priorityRating, loopingType);
				if (!success) {
					return false;
				}
				timeStretcher->setupCrossfadeFromCache(oldCache, cacheBytePos, sampleSourceNumChannels);
				goto timeStretchingConsidered;
			}
		}

		// Or, if a Cluster got stolen, we're in trouble
		else if (cache->writeBytePos < cacheBytePos) {
			bool success = stopUsingCache(guide, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
			if (!success) {
				return false;
			}
		}

		// Or if no Clusters got stolen, check some other stuff
		else {
			if (writingToCache) { // Writing

				if (cache->writeBytePos > cacheBytePos) { // Could this really happen?
					D_PRINTLN("cache written to by someone else");
					switchToReadingCacheFromWriting();
				}
			}

			else { // Reading

				// If cache is time-stretched and we're almost at the end of what was written, we'd better switch out of
				// cache-reading mode now so we can use that last little bit of the cache to crossfade smoothly out of
				// it
				if (timeStretchRatio != kMaxSampleValue && cache->writeBytePos < cacheEndPointBytes
				    && cache->writeBytePos < cacheLoopEndPointBytes
				    && cache->writeBytePos
				           < cacheBytePos
				                 + (TimeStretch::kDefaultFirstHopLength * kCacheByteDepth * sampleSourceNumChannels)) {

					// D_PRINTLN("avoiding click near end");

					bool success = stopReadingFromCache();
					if (!success) {
						return false;
					}

					// Beware - having just stopped reading from cache, our currentPlayPos might be way out of bounds,
					// potentially delivering even a negative sample / byte number. This needs to be maintained
					// cohesively until it's ultimately dealt with.

					success = weShouldBeTimeStretchingNow(sample, guide, numSamples, phaseIncrement, timeStretchRatio,
					                                      playDirection, priorityRating, loopingType);
					if (!success) {
						return false;
					}
					timeStretcher->setupCrossfadeFromCache(cache, cacheBytePos, sampleSourceNumChannels);

					// Check that all that setting up didn't steal any of our cache to the left of where we are now - in
					// which case we can't continue to write to it, and there's nothing else we want it for, so forget
					// about it. Also can't continue to write if doing linear interpolation now
					if (cache->writeBytePos < cacheBytePos
					    || (phaseIncrement != kMaxSampleValue
					        && interpolationBufferSize != kInterpolationMaxNumSamples)) {
						cache = nullptr;
					}

					// Or if we're still all good
					else {
						cache->setWriteBytePos(
						    cacheBytePos); // Re-write the last little bit of the cache, from where we are now
						writingToCache = true;
					}

					goto timeStretchingConsidered;
				}
			}
		}
	}

	// If not reading from a cache (but possibly writing to one)...
	if (!cache || writingToCache) {

		// If we should be time stretching now...
		if (timeStretchRatio != kMaxSampleValue) {
			bool stillGoing = weShouldBeTimeStretchingNow(sample, guide, numSamples, phaseIncrement, timeStretchRatio,
			                                              playDirection, priorityRating, loopingType);
			if (!stillGoing) {
				return false;
			}

			// If writing to cache, there's a chance that setting up time stretching and generating perc data could have
			// stolen Clusters, so we might have to stop writing to cache
			if (cache && writingToCache) {
				if (cache->writeBytePos < cacheBytePos) {
					bool success = stopUsingCache(guide, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
					if (!success) {
						return false;
					}
				}
			}
		}

		// If we shouldn't be time stretching now, but if it remains set up from before, stop it. We'd know there's no
		// cache in this case
		else {
			if (timeStretcher && !fudging) [[unlikely]] {

				// We're only allowed to stop once all the play-pos's line up, otherwise there's a big ol' click
				bool canExit;
#if TIME_STRETCH_ENABLE_BUFFER
				if (timeStretcher->buffer) {
					canExit = (timeStretcher->bufferFillingMode == BUFFER_FILLING_NEWER
					           && timeStretcher->olderHeadReadingFromBuffer
					           && timeStretcher->bufferWritePos == timeStretcher->olderBufferReadPos);
				}
				else
#endif
				{
					canExit = (currentPlayPos == timeStretcher->olderPartReader.currentPlayPos
					           && (!guide->sequenceSyncLengthTicks || !guide->getNumSamplesLaggingBehindSync(this)));
				}
				if (canExit) {
					D_PRINTLN("time stretcher no longer needed");
					endTimeStretching();
					bool stillGoing =
					    reassessReassessmentLocation(guide, sample,
					                                 priorityRating); // Got to - because time stretching affects the
					                                                  // SampleLowLevelReader's adherence to markers
					if (!stillGoing) {
						return false;
					}

					// Bugfix Sept 2020. Could end up beyond reassessmentLocation otherwise, violating assumptions if
					// playing Sample at non-native rate.
					stillGoing =
					    changeClusterIfNecessary(guide, sample, loopingType == LoopType::LOW_LEVEL, priorityRating);
					if (!stillGoing) {
						return false;
					}
					// Or, if changeClusterIfNecessary() returns false, should we continue outputting those zeros
					// through the interpolation buffer for a bit or something?
				}
			}
		}
	}

timeStretchingConsidered:

	int32_t bytesPerSample = sampleSourceNumChannels * sample->byteDepth;
	int32_t jumpAmount = sample->byteDepth * sampleSourceNumChannels * playDirection;
	int32_t* __restrict__ outputBufferWritePos = outputBuffer;
	int32_t numChannelsInOutputBuffer = (sampleSourceNumChannels == 2 && AudioEngine::renderInStereo) ? 2 : 1;
	int32_t whichKernel;
	uint64_t combinedIncrement;

	amplitude <<= 3;
	amplitudeIncrement <<= 3;

	// If pitch adjusting
	if (phaseIncrement != kMaxSampleValue) {
		amplitude <<= 1;
		amplitudeIncrement <<= 1;

		whichKernel = getWhichKernel(phaseIncrement);
	}

	// If time-stretching or reading a time-stretched cache
	if (timeStretcher) {
		amplitude <<= 1;
		amplitudeIncrement <<= 1;

		combinedIncrement = ((uint64_t)(uint32_t)timeStretchRatio * (uint32_t)phaseIncrement) >> 24;
	}

	// Loop over and over until we've read all the samples we want, until numSamples is 0

	// Replaying cached stuff!
	// again unlikely since this is the fast case
	if (cache && !writingToCache) [[unlikely]] {
		// if (!getRandom255()) D_PRINTLN("reading cache");

readCachedWindow:

		int32_t numSamplesThisCacheRead = numSamples;

		// If we've reached the loop end point...
		int32_t bytesTilLoopEndPoint = cacheLoopEndPointBytes - cacheBytePos;
		if (bytesTilLoopEndPoint <= 0) { // Might be less than 0 if it was just changed... although the code that does
			                             // that is suppose to also detect that we're past it and restart the loop...
			D_PRINTLN("Loop endpoint reached, reading cache");
			// Jump back to the loop start point. We'll find out in a moment whether that Cluster still exists (though
			// it will if the one we were just at existed)
			cacheBytePos -= cacheLoopLengthBytes;
			goto readCachedWindow;
		}

		// If we've reached the actual end of the (unlooped) waveform
		int32_t bytesTilWaveformEnd = cacheEndPointBytes - cacheBytePos;
		if (bytesTilWaveformEnd <= 0) { // Probably couldn't actually get below 0?
			// D_PRINTLN("waveform end reached, reading cache");
			return false;
		}

		// If we've reached the exact end of what's been written to the cache...
		int32_t bytesTilCacheEnd = cache->writeBytePos - cacheBytePos;
		if (bytesTilCacheEnd == 0) {

			// D_PRINTLN("Reached end of what's been cached");

			// If we're here, then timeStretchRatio should be kMaxSampleValue, and phaseIncrement should *not*
			if (ALPHA_OR_BETA_VERSION && timeStretchRatio != kMaxSampleValue) {
				FREEZE_WITH_ERROR("E240"); // This should have been caught and dealt with above
			}
			if (ALPHA_OR_BETA_VERSION && phaseIncrement == kMaxSampleValue) {
				FREEZE_WITH_ERROR("E241"); // If this were the case, there'd be no reason to have a cache
			}

			if (!stopReadingFromCache()) {
				return false;
			}

			// If linear interpolation, no cache writing (or anything) allowed
			if (interpolationBufferSize != kInterpolationMaxNumSamples) {
				cache = nullptr;
			}

			// Otherwise, record more data into the cache
			else {
				writingToCache = true;
			}

			// Now that we'll be reading the raw Sample source, we'll need to set up the reassessmentLocation and the
			// clusterStartLocation, so make this call. Also (I think) another reason for this call is that while
			// writing cache, low-level loop points aren't obeyed and we loop at the writing-to-cache level instead?
			bool stillGoing = reassessReassessmentLocation(
			    guide, sample,
			    priorityRating); // Returns false if fail, which can happen if we've actually ended up past the
			                     // finalClusterIndex cos we were reading cache before
			// TODO: need to call changeClusterIfNecessary()? Or are we guaranteed not to have passed that loop point /
			// reassessment location?
			if (!stillGoing) {
				return false;
			}

			// This step added Sept 2020 after finding another similar bug which made me fairly sure this needs to be
			// here, to ensure currentPlayPos isn't past the new reassessmentLocation
			stillGoing = changeClusterIfNecessary(guide, sample, loopingType == LoopType::LOW_LEVEL, priorityRating);
			if (!stillGoing) {
				return false;
			}

			// We know there's no time stretching - see above

			goto uncachedPlayback;
		}

		// This shouldn't happen - it gets checked for up at the start
		else if (ALPHA_OR_BETA_VERSION && bytesTilCacheEnd < 0) {
			FREEZE_WITH_ERROR("E164");
		}

		int32_t cachedClusterIndex = cacheBytePos >> Cluster::size_magnitude;
		int32_t bytePosWithinCluster = cacheBytePos & (Cluster::size - 1);

		Cluster* cacheCluster = cache->getCluster(cachedClusterIndex);
		if (ALPHA_OR_BETA_VERSION
		    && !cacheCluster) { // If it got stolen - but we should have already detected this above
			FREEZE_WITH_ERROR("E157");
		}
		int32_t* __restrict__ readPos = (int32_t*)&cacheCluster->data[bytePosWithinCluster - 4 + kCacheByteDepth];

		int32_t sampleRead[2]; // Somehow works out a tiny bit faster having it as an array

		sampleRead[0] = *readPos; // Do first read up here so there's time for the processor to access the memory

		int32_t bytesTilCacheClusterEnd = Cluster::size - bytePosWithinCluster;

		int32_t bytesTilThisWindowEnd = std::min(bytesTilCacheClusterEnd, bytesTilCacheEnd);
		bytesTilThisWindowEnd = std::min(bytesTilThisWindowEnd, bytesTilLoopEndPoint);
		bytesTilThisWindowEnd = std::min(bytesTilThisWindowEnd, bytesTilWaveformEnd);

		int32_t samplesTilThisWindowEnd;
		if constexpr (kCacheByteDepth == 3) {
			samplesTilThisWindowEnd =
			    (uint32_t)(bytesTilThisWindowEnd - 1) / (uint8_t)(sampleSourceNumChannels * kCacheByteDepth)
			    + 1; // Round up
		}
		else {
			samplesTilThisWindowEnd = bytesTilThisWindowEnd >> kCacheByteDepthMagnitude;
			if (sampleSourceNumChannels == 2) {
				samplesTilThisWindowEnd >>= 1;
			}
		}

		if (samplesTilThisWindowEnd < numSamplesThisCacheRead) {
			numSamplesThisCacheRead =
			    samplesTilThisWindowEnd; // Only do this after we're sure we're not cancelling caching
		}

		if (ALPHA_OR_BETA_VERSION && numSamplesThisCacheRead <= 0) {
			FREEZE_WITH_ERROR("E156");
		}

		// Ok, now we know how many samples we can read from the cache right now. Do it.
		// Note: I tried putting this loop in another function, but it just wasn't quite as fast, even with it set to
		// inline.
		int32_t const* const oscBufferEndNow =
		    outputBufferWritePos + numSamplesThisCacheRead * numChannelsInOutputBuffer;

		while (true) {

			int32_t existingValueL = *outputBufferWritePos;

			readPos = (int32_t*)((char*)readPos + kCacheByteDepth);

			if (sampleSourceNumChannels == 2) {
				sampleRead[1] = *readPos;
				readPos = (int32_t*)((char*)readPos + kCacheByteDepth);

				// If condensing to mono, do that now
				if (numChannelsInOutputBuffer == 1) {
					sampleRead[0] = ((sampleRead[0] >> 1) + (sampleRead[1] >> 1));
				}
			}

			amplitude += amplitudeIncrement;

			// Mono / left channel (or stereo condensed to mono)
			*outputBufferWritePos = multiply_accumulate_32x32_rshift32_rounded(existingValueL, sampleRead[0],
			                                                                   amplitude); // Yup, accumulate is faster
			outputBufferWritePos++;

			// Right channel. Surprisingly, putting this as an "else" inside the above "numChannels == 2" was slightly
			// slower
			if (numChannelsInOutputBuffer == 2) {
				int32_t existingValueR = *outputBufferWritePos;
				*outputBufferWritePos =
				    multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead[1], amplitude);
				outputBufferWritePos++;
			}

			if (outputBufferWritePos == oscBufferEndNow) {
				break;
			}

			sampleRead[0] = *readPos;
		}

		cacheBytePos += numSamplesThisCacheRead * kCacheByteDepth * sampleSourceNumChannels;

		// Need to also keep track of the un-cached play-pos so we can switch back if needed

		uint32_t cacheSamplePos = 0;
		if constexpr (kCacheByteDepth == 3) {
			cacheSamplePos = (uint32_t)cacheBytePos / kCacheByteDepth;
		}
		else {
			cacheSamplePos = cacheBytePos >> kCacheByteDepthMagnitude;
		}

		if (sampleSourceNumChannels == 2) {
			cacheSamplePos >>= 1;
		}
		uint64_t combinedIncrement = ((uint64_t)(uint32_t)phaseIncrement * (uint32_t)timeStretchRatio) >> 24;
		uint64_t uncachedSamplePosBig = (uint64_t)cacheSamplePos * combinedIncrement;
		int32_t uncachedSamplePos = (int32_t)(uncachedSamplePosBig >> 24) + cache->skipSamplesAtStart;
		int32_t uncachedBytePos = (playDirection == 1)
		                              ? sample->audioDataStartPosBytes + uncachedSamplePos * bytesPerSample
		                              : sample->audioDataStartPosBytes + sample->audioDataLengthBytes
		                                    - (uncachedSamplePos + 1) * bytesPerSample;

		int32_t uncachedClusterIndex = uncachedBytePos >> Cluster::size_magnitude;

		// Sometimes our cache will extend a little beyond the end of the waveform (to capture the interpolation or
		// time-stretching ring-out). It's basically ok for our current Cluster index and currentPlayPos to sit outside
		// the waveform - this will be dealt with if we do stop using the cache.

		// But, if we're more than 1 cluster outside of the waveform, let's just not be silly.
		if (uncachedClusterIndex < sample->getFirstClusterIndexWithAudioData() - 1
		    || uncachedClusterIndex > sample->getFirstClusterIndexWithNoAudioData()) {
			unassignAllReasons(false); // Remember, this doesn't cut the voice - just sets clusters[0] to NULL.
			currentPlayPos = nullptr;
		}

		// But if that hasn't happened...
		else {

			// Need to make sure we don't go past finalClusterIndex, or else, if cache is cancelled and loop points
			// obeyed again, we're in a Cluster we're not allowed in! Instead, stay in the Cluster we are allowed in,
			// currentPlayPos will end up outside the bounds of that Cluster, that'll soon be picked up on, and a loop
			// point will get obeyed or something.
			int32_t finalClusterIndex = guide->getFinalClusterIndex(sample, true);
			if ((uncachedClusterIndex - finalClusterIndex) * playDirection > 0) {
				uncachedClusterIndex = finalClusterIndex;
			}

			// If uncached Cluster has changed, update queue
			if (!clusters[0] || clusters[0]->clusterIndex != uncachedClusterIndex) {
				unassignAllReasons(false); // We're going to set new "reasons".

				int32_t nextUncachedClusterIndex = uncachedClusterIndex;
				for (int32_t l = 0; l < kNumClustersLoadedAhead; l++) {
					clusters[l] = sample->clusters.getElement(nextUncachedClusterIndex)
					                  ->getCluster(sample, nextUncachedClusterIndex, CLUSTER_ENQUEUE);
					if (!clusters[l]) {
						break;
					}
					if (nextUncachedClusterIndex == finalClusterIndex) {
						break; // If no more Clusters
					}
					nextUncachedClusterIndex += playDirection;
				}
			}

			if (clusters[0]) {
				oscPos = uncachedSamplePosBig & 16777215;
				int32_t uncachedBytePosWithinCluster = uncachedBytePos - uncachedClusterIndex * Cluster::size;
				currentPlayPos = &clusters[0]->data[uncachedBytePosWithinCluster];
				currentPlayPos = currentPlayPos - 4 + sample->byteDepth;
			}
			else {
				currentPlayPos = nullptr;
			}
		}

		numSamples -= numSamplesThisCacheRead;
		if (numSamples) {
			goto readCachedWindow;
		}
	}

	// Or, not replaying cached stuff (but potentially writing into the cache for next time)
	else [[likely]] {

uncachedPlayback:

		int32_t numSamplesThisUncachedRead = numSamples;

		char* cacheWritePos;

		// If there's a cache, prepare to write to it
		if (cache) {

			// If reached loop end, switch to playing the cached loop back
			int32_t cachingBytesTilLoopEnd = cacheLoopEndPointBytes - cache->writeBytePos;
			if (cachingBytesTilLoopEnd
			    <= 0) { // Might be less than 0 if it was just changed... although the code that does that is suppose to
				        // also detect that we're past it and restart the loop...
				D_PRINTLN("Loop endpoint reached, writing cache");
				switchToReadingCacheFromWriting();
				goto readCachedWindow;
			}

			// If reached end of actual sample waveform, we're done! We know we're not looping, cos the above condition
			// would have triggered
			int32_t cachingBytesTilWaveformEnd = cacheEndPointBytes - cache->writeBytePos;
			if (cachingBytesTilWaveformEnd <= 0) { // Probably couldn't actually get below 0?
				// D_PRINTLN("waveform end reached, writing cache");
				return false;
			}

			int32_t cacheClusterIndex = cache->writeBytePos >> Cluster::size_magnitude;
			int32_t bytePosWithinCluster = cache->writeBytePos & (Cluster::size - 1);

			// If just entering brand new Cluster, we need to allocate it first
			const bool condition = kCacheByteDepth == 3
			                           ? bytePosWithinCluster < sampleSourceNumChannels * kCacheByteDepth
			                           : bytePosWithinCluster == 0;
			if (condition) {

				bool setupSuccess = cache->setupNewCluster(cacheClusterIndex);
				if (!setupSuccess) {
					// Cancel cache writing. Everything else is still the same - we weren't *reading* the cache,
					// remember
					bool stopSuccess =
					    stopUsingCache(guide, sample, priorityRating,
					                   loopingType == LoopType::LOW_LEVEL); // Want to obey loop points now
					if (!stopSuccess) {
						return false;
					}
					goto uncachedPlaybackNotWriting;
				}
			}

			Cluster* cacheCluster = cache->getCluster(cacheClusterIndex);
			if (ALPHA_OR_BETA_VERSION && !cacheCluster) {
				// Check that the Cluster hasn't been stolen - but this should have been detected right at the start
				FREEZE_WITH_ERROR("E166");
			}
			cacheWritePos = &cacheCluster->data[bytePosWithinCluster];

			int32_t cachingBytesTilClusterEnd = Cluster::size - bytePosWithinCluster;
			int32_t cachingBytesTilUncachedReadEnd = std::min(cachingBytesTilClusterEnd, cachingBytesTilLoopEnd);
			cachingBytesTilUncachedReadEnd = std::min(cachingBytesTilUncachedReadEnd, cachingBytesTilWaveformEnd);

			int32_t cachingSamplesTilUncachedReadEnd = 0;

			if constexpr (kCacheByteDepth == 3) {
				cachingSamplesTilUncachedReadEnd = (uint32_t)(cachingBytesTilUncachedReadEnd - 1)
				                                       / (uint8_t)(sampleSourceNumChannels * kCacheByteDepth)
				                                   + 1; // Round up
			}
			else {
				cachingSamplesTilUncachedReadEnd = cachingBytesTilUncachedReadEnd >> kCacheByteDepthMagnitude;
				if (sampleSourceNumChannels == 2) {
					cachingSamplesTilUncachedReadEnd >>= 1;
				}
			}

			if (cachingSamplesTilUncachedReadEnd < numSamplesThisUncachedRead) {
				numSamplesThisUncachedRead =
				    cachingSamplesTilUncachedReadEnd; // Only do this after we're sure we're not cancelling caching
			}

			if (ALPHA_OR_BETA_VERSION && numSamplesThisUncachedRead <= 0) {
				FREEZE_WITH_ERROR("E155");
			}
		}

		// Or if no cache to write to - so we're neither reading nor writing...
		else {
uncachedPlaybackNotWriting:

			// If time stretching but not synced, now is the time to check loop / end point
			if (timeStretcher && !guide->sequenceSyncLengthTicks) {
				int32_t reassessmentPos = guide->getBytePosToEndOrLoopPlayback();

#if ALPHA_OR_BETA_VERSION
				int32_t count = 0;
#endif

				// Do quick check before we commit to a divide operation
				int64_t combinedIncrementingThisUncachedRead = combinedIncrement * numSamplesThisUncachedRead;

assessLoopPointAgainTimestretched:
				int64_t samplePosBigAfterThisUncachedRead =
				    timeStretcher->samplePosBig + combinedIncrementingThisUncachedRead * playDirection;
				if (playDirection == -1) {
					samplePosBigAfterThisUncachedRead += 16777215;
				}
				int32_t bytePosAfterThisUncachedRead =
				    (int32_t)(samplePosBigAfterThisUncachedRead >> 24) * bytesPerSample
				    + sample->audioDataStartPosBytes;

				int32_t overshootBytes = (bytePosAfterThisUncachedRead - reassessmentPos) * playDirection;

				// If we will have reached the reassessment-point, by the end of this "uncached read"...
				// unlikely to match rohan's check from before - otherwise we'll speculatively execute anyway
				if (overshootBytes > 0) [[unlikely]] {

					// Ok, we passed the quick check, so now do the divide operation
					int32_t reassessmentPosBytesRelativeToAudioDataStart =
					    reassessmentPos - sample->audioDataStartPosBytes;
					int32_t loopEndAtSample =
					    (reassessmentPosBytesRelativeToAudioDataStart >= 0)
					        ? (uint32_t)reassessmentPosBytesRelativeToAudioDataStart / (uint8_t)bytesPerSample
					        : -1; // This is plausible, cos going in reverse, the "end pos" is -1 sample

					int64_t combinedIncrementingLeftToDo =
					    ((int64_t)loopEndAtSample << 24) - timeStretcher->samplePosBig;
					int64_t combinedIncrementingLeftToDoAbsolute = combinedIncrementingLeftToDo * playDirection;

					// Maybe we're at the reassessment-point right now, so need to reassess...
					if (combinedIncrementingLeftToDoAbsolute < (int64_t)
					        combinedIncrement) { // It seems that without the (int64_t) enforced, it was behaving wrong

						// If want to actually loop... (Remember though, this whole bit doesn't apply to synced samples
						// / AudioClips.)
						if (loopingType != LoopType::NONE) { // For either type of looping

							D_PRINTLN("loop point reached, timestretching");

							int32_t newSamplePos =
							    (uint32_t)(guide->getBytePosToStartPlayback(true) - sample->audioDataStartPosBytes)
							    / (uint8_t)bytesPerSample; // Not including the overshoot yet
							int64_t newSamplePosBigOvershot =
							    ((int64_t)newSamplePos << 24) - combinedIncrementingLeftToDo;

							timeStretcher->reInit(newSamplePosBigOvershot, guide, this, sample, sampleSourceNumChannels,
							                      timeStretchRatio, phaseIncrement, combinedIncrement, playDirection,
							                      loopingType, priorityRating);

#if ALPHA_OR_BETA_VERSION
							if (count >= 1024) {
								FREEZE_WITH_ERROR("E169");
							}
							count++;
#endif
							goto assessLoopPointAgainTimestretched;
						}

						// Or, if want to stop at this reassessment point we just reached, that's easy
						else {
							return false;
						}
					}

					// Or otherwise, just shorten this uncached-read to stop right at the reassessment-point, so we can
					// reassess immediately after
					else {
						int32_t combinedIncrementsLeft =
						    (uint64_t)combinedIncrementingLeftToDoAbsolute / combinedIncrement;
						if (ALPHA_OR_BETA_VERSION && combinedIncrementsLeft > numSamplesThisUncachedRead) {
							D_PRINTLN((const char*)combinedIncrementsLeft);
							FREEZE_WITH_ERROR("E151");
						}
						numSamplesThisUncachedRead = combinedIncrementsLeft;
					}
				}
			}
		}

		numSamples -= numSamplesThisUncachedRead; // Do it now because we're about to start decrementing
		                                          // numSamplesThisUncachedRead

		// If no time stretching
		// unlikely as it's the fast happy path and we don't struggle with it
		if (!timeStretcher) [[unlikely]] {

readNonTimestretched:

			int32_t numSamplesThisNonTimestretchedRead = numSamplesThisUncachedRead;

			bool stillActive = considerUpcomingWindow(
			    guide, sample, &numSamplesThisNonTimestretchedRead, phaseIncrement, loopingType != LoopType::NONE,
			    interpolationBufferSize, (cache != nullptr),
			    priorityRating); // Keep it reading silence forever so we can definitely fill up the cache
			if (!stillActive) {
				return false;
			}

			// No pitch adjustment - in which case we know we're not writing to cache either
			if (phaseIncrement == kMaxSampleValue) {

				readSamplesNative((int32_t**)&outputBufferWritePos, numSamplesThisNonTimestretchedRead, sample,
				                  jumpAmount, sampleSourceNumChannels, numChannelsInOutputBuffer, &amplitude,
				                  amplitudeIncrement);
			}

			// Yes pitch adjustment - meaning we might be wanting to write to a cache, too...
			else {

				// That call to considerUpcomingWindow() just might have led to our cache having Clusters stolen!
				if (cache && cache->writeBytePos < cacheBytePos) {
					bool success = stopUsingCache(guide, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
					if (!success) {
						return false;
					}
				}

				bool doneAnySamplesYet = false;

				// This stuff is in its own function rather than here in VoiceSample because for some reason it's faster
				// (re-check that?)
				readSamplesResampled((int32_t**)&outputBufferWritePos, numSamplesThisNonTimestretchedRead, sample,
				                     jumpAmount, sampleSourceNumChannels, numChannelsInOutputBuffer, phaseIncrement,
				                     &amplitude, amplitudeIncrement, interpolationBufferSize, (cache != nullptr),
				                     &cacheWritePos, &doneAnySamplesYet, NULL, false, whichKernel);
			}

			if (cache) {
				cacheBytePos += numSamplesThisNonTimestretchedRead * kCacheByteDepth * sampleSourceNumChannels;
				cache->writeBytePos = cacheBytePos; // These two were and are now still the same
			}

			numSamplesThisUncachedRead -= numSamplesThisNonTimestretchedRead;
			if (numSamplesThisUncachedRead) {
				goto readNonTimestretched;
			}
		}

		// Or, if yes time stretching
		else [[likely]] { // TODO: move this all into the TimeStretcher class?

			// AudioEngine::logAction("yes timestretching");

			int32_t* timeStretchResultWritePos;
			int32_t numChannelsInTimeStretchResult;

			int32_t* const tempBuffer = spareRenderingBuffer[1];

			if (cache) { // If writing cache...

				// Because we're writing cache, and if both play-heads have finished up, clearing that cache is all
				// that's actually required!
				if (!timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
				    && !timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {

					memset(cacheWritePos, 0, numSamplesThisUncachedRead * kCacheByteDepth * sampleSourceNumChannels);

					cacheBytePos += numSamplesThisUncachedRead * kCacheByteDepth * sampleSourceNumChannels;
					cache->writeBytePos = cacheBytePos; // These two were and are now still the same

					goto finishedTimestretchedRead;
				}

				timeStretchResultWritePos = tempBuffer;

				// Have to clear the cache
				memset(timeStretchResultWritePos, 0, numSamplesThisUncachedRead * 4 * sampleSourceNumChannels);

				numChannelsInTimeStretchResult = sampleSourceNumChannels;
			}
			else {
				timeStretchResultWritePos = outputBufferWritePos;
				numChannelsInTimeStretchResult = numChannelsInOutputBuffer;
			}

			int32_t numSamplesThisUncachedReadUntouched =
			    numSamplesThisUncachedRead; // This one won't get decremented between multiple timestretch iterations

readTimestretched:
			int32_t numSamplesThisTimestretchedRead = numSamplesThisUncachedRead;

			// Now, perform the actual time stretching on the contents of the bands
			GeneralMemoryAllocator::get().checkStack("timestretch");

			if (timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {

				bool wantToDoHopNow = (timeStretcher->samplesTilHopEnd <= 0);

				// Basically, if other hops have already happened in this routine, we want to defer doing one, to spread
				// the CPU load. But, the more times we've been skipped in this way, the more we start insisting that
				// yes, it's our turn.
				bool allowedToDoHop = (AudioEngine::numHopsEndedThisRoutineCall < 3
				                       && (uint32_t)timeStretcher->numTimesMissedHop
				                              >= ((uint32_t)AudioEngine::numHopsEndedThisRoutineCall << 1));

				if (wantToDoHopNow) {
					if (allowedToDoHop) {
						bool success = timeStretcher->hopEnd(guide, this, sample, sampleSourceNumChannels,
						                                     timeStretchRatio, phaseIncrement, combinedIncrement,
						                                     playDirection, loopingType, priorityRating);
						if (!success) {
							return false; // Can fail if tried to jump to a bit in an unloaded Cluster. Shouldn't
							              // actually happen
						}

						// Check this again, cos newer play-head can become inactive in hopEnd(). This probably isn't
						// really crucial. Added June 2019
						if (!cache && loopingType == LoopType::NONE
						    && !timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
						    && !timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {
							return false;
						}
					}
					else {
						timeStretcher->numTimesMissedHop++;
					}
				}

				if (allowedToDoHop) {
					numSamplesThisTimestretchedRead =
					    std::min((int32_t)numSamplesThisTimestretchedRead, timeStretcher->samplesTilHopEnd);
				}
			}

			// TODO: would I save CPU if I also made it stop right at the end of the crossfade?

			int32_t newerSourceAmplitudeNow;
			int32_t newerAmplitudeIncrementNow;
			int32_t olderSourceAmplitudeNow;
			int32_t olderAmplitudeIncrementNow;
			bool olderPlayHeadAudibleHere = (timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
			                                 && timeStretcher->crossfadeProgress < kMaxSampleValue);

			bool didShortening1 = false;
#if TIME_STRETCH_ENABLE_BUFFER
			if (timeStretcher->newerHeadReadingFromBuffer
			    && !(timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
			         && timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER)) {

				int32_t samplesTilChangeover = (timeStretcher->bufferWritePos - timeStretcher->newerBufferReadPos)
				                               & (TimeStretch::BUFFER_SIZE - 1);
				if (numSamplesThisTimestretchedRead > samplesTilChangeover) {
					D_PRINTLN("shortening 1");
					numSamplesThisTimestretchedRead = samplesTilChangeover;
					didShortening1 = true;
				}
			}

			if (olderPlayHeadAudibleHere) {

				if (timeStretcher->olderHeadReadingFromBuffer
				    && timeStretcher->bufferFillingMode != BUFFER_FILLING_NEWER) {
					int32_t samplesTilChangeover = (timeStretcher->bufferWritePos - timeStretcher->olderBufferReadPos)
					                               & (TimeStretch::BUFFER_SIZE - 1);
					if (numSamplesThisTimestretchedRead > samplesTilChangeover) {
						D_PRINTLN("shortening 2");
						numSamplesThisTimestretchedRead = samplesTilChangeover;
					}
				}
			}
#endif

			int32_t preCacheAmplitude, preCacheAmplitudeIncrement;

			// If there's a cache, we don't want the per-play-head functions to apply the overall voice amplitude
			// envelope - we'll do that at the end when we copy from the cache into the output buffer
			if (cache) {
				preCacheAmplitude = 2147483647;
				preCacheAmplitudeIncrement = 0;
			}
			else {
				// We >>1 to match the fact that in the yes-cache case, above, the max we can set this to is 2147483647,
				// and we compensate for both these things by having <<1'd amplitude for time-stretching, at the start
				// of this func. This isn't quite ideal
				preCacheAmplitude = amplitude >> 1;
				preCacheAmplitudeIncrement = amplitudeIncrement >> 1;
			}

			// If the older play-head is still fading out, which means we need to work out the crossfade envelope too...
			if (olderPlayHeadAudibleHere) {

				// Use linear crossfades. These sound less jarring when doing short hops. Square root ones sound better
				// for longer hops, with more difference in material between hops (i.e. time more stretched)
				int32_t newerHopAmplitudeNow = timeStretcher->crossfadeProgress << 7;
				int32_t olderHopAmplitudeNow = 2147483647 - newerHopAmplitudeNow;

				timeStretcher->crossfadeProgress += timeStretcher->crossfadeIncrement * numSamplesThisTimestretchedRead;

				int32_t newerHopAmplitudeAfter = lshiftAndSaturate<7>(timeStretcher->crossfadeProgress);

				int32_t newerHopAmplitudeIncrement =
				    (int32_t)(newerHopAmplitudeAfter - newerHopAmplitudeNow) / (int32_t)numSamplesThisTimestretchedRead;
				int32_t olderHopAmplitudeIncrement = -newerHopAmplitudeIncrement;

				int32_t hopAmplitudeChange = multiply_32x32_rshift32(preCacheAmplitude, newerHopAmplitudeIncrement)
				                             << 1;

				newerAmplitudeIncrementNow = preCacheAmplitudeIncrement + hopAmplitudeChange;
				newerSourceAmplitudeNow = multiply_32x32_rshift32(preCacheAmplitude, newerHopAmplitudeNow) << 1;

				olderAmplitudeIncrementNow = preCacheAmplitudeIncrement - hopAmplitudeChange;
				olderSourceAmplitudeNow = multiply_32x32_rshift32(preCacheAmplitude, olderHopAmplitudeNow) << 1;
			}

			// Or, if we're just hearing the newer play-head
			else {
				newerSourceAmplitudeNow = preCacheAmplitude;
				newerAmplitudeIncrementNow = preCacheAmplitudeIncrement;
				// No need to set the "older" ones, cos that's not going to be rendered
			}

#if TIME_STRETCH_ENABLE_BUFFER
			bool swappingOrder =
			    (timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]
			     && timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER
			     && (timeStretcher
			             ->playHeadStillActive[PLAY_HEAD_OLDER])); // || !timeStretcher->newerHeadReadingFromBuffer));
#else
			bool swappingOrder = false;
#endif
			if (swappingOrder) {
				goto considerOlderHead;
			}

			// Read newer play-head
readNewerHead:
			if (timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {
#if TIME_STRETCH_ENABLE_BUFFER
				// If reading buffer...
				if (timeStretcher->newerHeadReadingFromBuffer) {
					timeStretcher->readFromBuffer(
					    timeStretchResultWritePos, writePosNowR, numSamplesThisTimestretchedRead,
					    sampleSourceNumChannels, numChannelsInOutputBuffer, newerSourceAmplitudeNow,
					    newerAmplitudeIncrementNow, &timeStretcher->newerBufferReadPos, writeIncrement);
				}

				// Or if reading normal unbuffered Cluster data...
				else
#endif
				{
					bool success = readSamplesForTimeStretching(
					    timeStretchResultWritePos, guide, sample, numSamplesThisTimestretchedRead,
					    sampleSourceNumChannels, numChannelsInTimeStretchResult, phaseIncrement,
					    newerSourceAmplitudeNow, newerAmplitudeIncrementNow, (loopingType == LoopType::LOW_LEVEL),
					    jumpAmount, interpolationBufferSize, timeStretcher,
#if TIME_STRETCH_ENABLE_BUFFER
					    (timeStretcher->bufferFillingMode == BUFFER_FILLING_NEWER),
#else
					    false,
#endif
					    PLAY_HEAD_NEWER, whichKernel, priorityRating);

					if (!success) {
						return false;
					}
				}
			}
			if (swappingOrder) {
				goto headsFinishedReading;
			}

considerOlderHead:
			// Read older play-head if still active
			if (olderPlayHeadAudibleHere) {

				// If reading buffer...
				if (timeStretcher->olderHeadReadingFromBuffer) {
					timeStretcher->readFromBuffer(timeStretchResultWritePos, numSamplesThisTimestretchedRead,
					                              sampleSourceNumChannels, numChannelsInTimeStretchResult,
					                              olderSourceAmplitudeNow, olderAmplitudeIncrementNow,
					                              &timeStretcher->olderBufferReadPos);
				}

				// Or if reading normal Cluster data...
				else {
readOlderHeadUnbuffered:
					bool success = timeStretcher->olderPartReader.readSamplesForTimeStretching(
					    timeStretchResultWritePos, guide, sample, numSamplesThisTimestretchedRead,
					    sampleSourceNumChannels, numChannelsInTimeStretchResult, phaseIncrement,
					    olderSourceAmplitudeNow, olderAmplitudeIncrementNow, (loopingType == LoopType::LOW_LEVEL),
					    jumpAmount, interpolationBufferSize, timeStretcher,
#if TIME_STRETCH_ENABLE_BUFFER
					    (timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER),
#else
					    false,
#endif
					    PLAY_HEAD_OLDER, whichKernel, priorityRating);

					if (!success) {
						return false;
					}
				}

				// If just stopped being active...
				if (timeStretcher->crossfadeProgress >= kMaxSampleValue
#if TIME_STRETCH_ENABLE_BUFFER
				    && !(timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER
				         && !timeStretcher->newerHeadReadingFromBuffer)
#endif
				) {
					// timeStretcher->olderPartReader.unassignAllReasons();
					// if (olderPlayHeadActive) D_PRINTLN("older head faded out");
					timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER] = false;
				}

				if (swappingOrder) {
					goto readNewerHead;
				}
			}
			else {
				if (timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]) {
					olderSourceAmplitudeNow = 0;
					olderAmplitudeIncrementNow = 0;
					// D_PRINTLN("doing special thing!!!");
					goto readOlderHeadUnbuffered;
				}
			}

headsFinishedReading:

			timeStretcher->samplePosBig += (int64_t)combinedIncrement * numSamplesThisTimestretchedRead * playDirection;

#if TIME_STRETCH_ENABLE_BUFFER
			if (timeStretcher->bufferFillingMode != BUFFER_FILLING_OFF) {

				if (timeStretcher->newerHeadReadingFromBuffer
				    && timeStretcher->newerBufferReadPos == timeStretcher->bufferWritePos
				    && !(timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
				         && timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER)) {

					D_PRINTLN("switch to filling newerrrrrrrrrrrrrrrr");
					// if (timeStretcher->bufferFillingMode == BUFFER_FILLING_OLDER) D_PRINTLN(" - was filling older");
					timeStretcher->newerHeadReadingFromBuffer = false;
					timeStretcher->bufferFillingMode = BUFFER_FILLING_NEWER;
					cloneFrom(&timeStretcher->olderPartReader, false);
					if (!clusters[0])
						D_PRINTLN("no clusters[0]");
				}

				else if (timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
				         && timeStretcher->olderHeadReadingFromBuffer
				         && timeStretcher->olderBufferReadPos == timeStretcher->bufferWritePos
				         && timeStretcher->bufferFillingMode != BUFFER_FILLING_NEWER) {
					timeStretcher->olderHeadReadingFromBuffer = false;
					timeStretcher->bufferFillingMode = BUFFER_FILLING_OLDER;
					// timeStretcher->olderPartReader.cloneFrom(this, false);
					D_PRINTLN("switch to filling older");
				}
			}
#endif

			if (!cache && loopingType == LoopType::NONE && !timeStretcher->playHeadStillActive[PLAY_HEAD_OLDER]
			    && !timeStretcher->playHeadStillActive[PLAY_HEAD_NEWER]) {
				return false;
			}

			timeStretcher->samplesTilHopEnd -= numSamplesThisTimestretchedRead;

			if (!cache) {
				amplitude += amplitudeIncrement * numSamplesThisTimestretchedRead;
			}

			numSamplesThisUncachedRead -= numSamplesThisTimestretchedRead;

			if (numSamplesThisUncachedRead) {
				timeStretchResultWritePos += numSamplesThisTimestretchedRead * numChannelsInTimeStretchResult;
				goto readTimestretched;
			}

			// If we were writing to a temp buffer instead of the output buffer because we need to cache,
			// now we copy from the temp buffer to both the cache and the output buffer
			if (cache) {
				int32_t* __restrict__ tempBufferReadPos = tempBuffer;

				// Pre-fetch some memory
				int32_t sampleRead[2];
				sampleRead[0] = *tempBufferReadPos;
				int32_t existingValueL = *outputBufferWritePos;

				int32_t const* const outputBufferEndNow =
				    outputBufferWritePos + numSamplesThisUncachedReadUntouched * numChannelsInOutputBuffer;

				// We'll do an ugly hack here - fudging is where a TimeStretcher is added just at the end of a
				// play-through, to do a crossfade. This leads to a rare case where if we're recording a cache, our
				// whether-there's-a-TimeStretcher changes, but we still keep the same cache. So, we have to compensate
				// for a difference in cached volume.
				bool fudgingNow = fudging;

				char* __restrict__ cacheWritePosNow = cacheWritePos;

				// Check that all of our sample-reading above hasn't stolen any of our cache Clusters.
				if (cache->writeBytePos != cacheBytePos) {
					bool success = stopUsingCache(guide, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
					if (!success) {
						return false; // Is this ideal? Didn't give much consideration when I wrote this line
					}
					cacheWritePosNow = (char*)UNUSED_MEMORY_SPACE_ADDRESS;
				}

				while (true) {
					tempBufferReadPos++;

					amplitude += amplitudeIncrement;

					int32_t forCache = sampleRead[0] << 1;

					*cacheWritePosNow = ((char*)&forCache)[1];
					cacheWritePosNow++;
					*cacheWritePosNow = ((char*)&forCache)[2];
					cacheWritePosNow++;
					*cacheWritePosNow = ((char*)&forCache)[3];
					cacheWritePosNow++;

					if (sampleSourceNumChannels == 2) {
						sampleRead[1] = *tempBufferReadPos;
						tempBufferReadPos++;

						// If condensing to mono, do that now
						if (numChannelsInOutputBuffer == 1) {
							sampleRead[0] = ((sampleRead[0] >> 1) + (sampleRead[1] >> 1));
						}

						int32_t forCache = sampleRead[1] << 1;

						*cacheWritePosNow = ((char*)&forCache)[1];
						cacheWritePosNow++;
						*cacheWritePosNow = ((char*)&forCache)[2];
						cacheWritePosNow++;
						*cacheWritePosNow = ((char*)&forCache)[3];
						cacheWritePosNow++;
					}

					// Mono / left channel (or stereo condensed to mono)
					*outputBufferWritePos = multiply_accumulate_32x32_rshift32_rounded(
					    existingValueL, sampleRead[0], amplitude); // amplitude is modified above
					outputBufferWritePos++;

					// Right channel
					if (numChannelsInOutputBuffer == 2) {
						int32_t existingValueR = *outputBufferWritePos;
						*outputBufferWritePos =
						    multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead[1], amplitude);
						outputBufferWritePos++;
					}

					if (outputBufferWritePos == outputBufferEndNow) {
						break;
					}

					sampleRead[0] = *tempBufferReadPos;
					existingValueL = *outputBufferWritePos;
				}

				if (cache) { // Might not be true anymore if we had to abandon the cache just above cos it got Clusters
					         // stolen.
					cacheWritePos =
					    cacheWritePosNow; // Not necessary I don't think - cacheWritePos doesn't get used again does it?

					cacheBytePos += numSamplesThisUncachedReadUntouched * kCacheByteDepth * sampleSourceNumChannels;
					cache->writeBytePos = cacheBytePos; // These two were and are now still the same
				}
			}

			// Otherwise, just have to update some stuff
			else {
				outputBufferWritePos += numSamplesThisUncachedReadUntouched * numChannelsInOutputBuffer;
			}

			// AudioEngine::logAction("/");
		}

finishedTimestretchedRead:
		// numSamples -= numSamplesThisUncachedRead; // No, this was now done above

		// If need to go again, to write to a different cache Cluster...
		if (numSamples) {
			goto uncachedPlayback;
		}
	}

	doneFirstRenderYet = true;
	return true;
}

// Returns false if became inactive
bool VoiceSample::sampleZoneChanged(SamplePlaybackGuide* voiceSource, Sample* sample, bool reversed,
                                    MarkerType markerType, LoopType loopingType, int32_t priorityRating,
                                    bool forAudioClip) {

	if (cache && cache->reversed != reversed) {
		// If the reversing has changed, then the cache is no longer valid
		bool success = stopUsingCache(voiceSource, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
		if (!success) {
			return false;
		}
	}
	// If cache, then update cache loop points - but not if it was the start marker that was moved, cos that means we'll
	// stop using cache altogether
	if (cache && markerType != MarkerType::START) {
		setupCacheLoopPoints(voiceSource, sample, loopingType);
	}

	if (markerType == MarkerType::START) {

		// If cache...
		if (cache) {
			bool success = stopUsingCache(voiceSource, sample, priorityRating, loopingType == LoopType::LOW_LEVEL);
			if (!success) {
				return false;
			}
		}

		// If no cache, no action necessary!
	}

	else if (markerType == MarkerType::LOOP_START) {
		// Everything's fine
	}

	else if (markerType == MarkerType::LOOP_END) {

		D_PRINTLN("MarkerType::LOOP_END");
		// If cache...
		if (cache) {

			// If we've shot past loop end point...
			if (cacheBytePos >= cacheLoopEndPointBytes) {
loopBackToStartCached:
				// Whether reading or writing, go back and start reading from loop start
				switchToReadingCacheFromWriting();
				cacheBytePos = cacheLoopEndPointBytes - cacheLoopLengthBytes; // This is allowed to be a bit rough
				// In a perfect world we'd check the Clusters are loaded and stuff, but that'll get checked soon enough,
				// and what would we do if they weren't loaded anyway
			}
		}

		// Or if no cache...
		else {

			if (timeStretcher) {
				D_PRINTLN("timeStretcher");

				if (((VoiceSamplePlaybackGuide*)voiceSource)->shouldObeyLoopEndPointNow()) {
					int32_t bytePos = timeStretcher->getSamplePos(voiceSource->playDirection)
					                      * (sample->byteDepth * sample->numChannels)
					                  + sample->audioDataStartPosBytes;
					int32_t overshootBytes = (bytePos - ((VoiceSamplePlaybackGuide*)voiceSource)->loopEndPlaybackAtByte)
					                         * voiceSource->playDirection;

					if (overshootBytes >= 0) {
						goto loopBackToStartTimeStretched;
					}
				}
			}
			else {
				D_PRINTLN("no timeStretcher");

				// If we've shot past the loop end point...
				if (((VoiceSamplePlaybackGuide*)voiceSource)->shouldObeyLoopEndPointNow()
				    && (int32_t)(getPlayByteLowLevel(sample, voiceSource)
				                 - ((VoiceSamplePlaybackGuide*)voiceSource)->loopEndPlaybackAtByte)
				               * voiceSource->playDirection
				           >= 0) {
					D_PRINTLN("shot past");
					goto loopBackToStartUncached;
				}
				else {
					goto justDoReassessment;
				}
			}
		}
	}

	else if (markerType == MarkerType::END) {

		// If cache...
		if (cache) {

			// If we've shot past end point...
			if (cacheBytePos >= cacheEndPointBytes) {
				// If we're looping, restart it
				if (loopingType != LoopType::NONE) {
					goto loopBackToStartCached;
				}
				else {
					return false;
				}
			}
		}

		// Or if no cache...
		else {

			if (timeStretcher) {

				if (voiceSource->endPlaybackAtByte
				    && (forAudioClip
				        || !((VoiceSamplePlaybackGuide*)voiceSource)
				                ->noteOffReceived)) { // Wait, wouldn't there always be a
					                                  // voiceSource->endPlaybackAtByte()?
					int32_t bytePos = timeStretcher->getSamplePos(voiceSource->playDirection)
					                      * (sample->byteDepth * sample->numChannels)
					                  + sample->audioDataStartPosBytes;
					int32_t overshootBytes = (bytePos - voiceSource->endPlaybackAtByte) * voiceSource->playDirection;

					if (overshootBytes >= 0) {

						// If we're looping, restart it
						if (loopingType != LoopType::NONE) {
							goto loopBackToStartTimeStretched;
						}

						// Otherwise, stop it
						else {
							return false;
						}
					}
				}
			}

			else {
				// If we've shot past the end...
				if ((int32_t)(getPlayByteLowLevel(sample, voiceSource) - voiceSource->endPlaybackAtByte)
				        * voiceSource->playDirection
				    >= 0) {

					// If we're looping, restart it
					if (loopingType != LoopType::NONE) {
loopBackToStartUncached:
						unassignAllReasons(false);
						setupClusersForInitialPlay(voiceSource, sample, 0, true, priorityRating);
					}

					// Otherwise, stop it
					else {
						return false;
					}
				}
				// Or if everything's still fine...
				else {
justDoReassessment:
					return reassessReassessmentLocation(voiceSource, sample, priorityRating);
					// I think this function has already taken care of making sure we're not past the new
					// reassessmentLocation...
				}
			}
		}
	}

	if (false) {
loopBackToStartTimeStretched:
		unassignAllReasons(false);

		endTimeStretching(); // It'll get started again at next render

		// Must call this before ending time stretching, cos the presence of the TimeStretcher causes the
		// SampleLowLevelReader to ignore markers, which is how we want it (no, wait, that's wrong I think? I've undone
		// that for now anyway)
		setupClusersForInitialPlay(voiceSource, sample, 0, true, priorityRating);
	}

	return true;
}

int32_t VoiceSample::getPlaySample(Sample* sample, SamplePlaybackGuide* guide) {
	if (timeStretcher) {
		return timeStretcher->getSamplePos(guide->playDirection);
	}

	else {
		int32_t bytePosFromAudioDataStart = getPlayByteLowLevel(sample, guide) - sample->audioDataStartPosBytes;
		return bytePosFromAudioDataStart / (sample->byteDepth * sample->numChannels);
	}
}

void VoiceSample::switchToReadingCacheFromWriting() {
	D_PRINTLN("switchToReadingCacheFromWriting");
	writingToCache = false;
	endTimeStretching();
}

// If returns false, means everything's failed badly and must cut whole VoiceSource (instantUnassign)
bool VoiceSample::possiblySetUpCache(SampleControls* sampleControls, SamplePlaybackGuide* guide, int32_t phaseIncrement,
                                     int32_t timeStretchRatio, int32_t priorityRating, LoopType loopingType) {

	if (phaseIncrement == kMaxSampleValue) {
		return true;
	}
	if (guide->sequenceSyncLengthTicks && (playbackHandler.isExternalClockActive())) {
		return true; // No syncing to external clock
	}
	if (sampleControls->interpolationMode != InterpolationMode::SMOOTH) {
		return true;
	}

	bool mayCreate = (sampleControls->getInterpolationBufferSize(phaseIncrement) == kInterpolationMaxNumSamples);
	cache = ((Sample*)(guide->audioFileHolder->audioFile))
	            ->getOrCreateCache((SampleHolder*)guide->audioFileHolder, phaseIncrement, timeStretchRatio,
	                               guide->playDirection == -1, mayCreate, &writingToCache);

	if (cache) {
		// D_PRINTLN("cache gotten");
		cacheBytePos = 0;

		setupCacheLoopPoints(guide, (Sample*)guide->audioFileHolder->audioFile, loopingType);
		bool result = reassessReassessmentLocation(guide, (Sample*)guide->audioFileHolder->audioFile, priorityRating);
		// Probably no need to check we haven't shot past the new reassessmentLocation, since surely that's going to be
		// further into the future now we're not obeying loop points at low level?
		if (!result) {
			return false;
		}
	}

	return true;
}
