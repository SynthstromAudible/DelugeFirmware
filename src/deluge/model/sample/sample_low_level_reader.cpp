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

#pragma GCC push_options
#pragma GCC target("fpu=neon")

#include "model/sample/sample_low_level_reader.h"
#include "model/sample/sample.h"
#include "storage/cluster/cluster.h"
#include "io/uart/uart.h"
#include "storage/audio/audio_file_manager.h"
#include "hid/display/numeric_driver.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "model/voice/voice.h"
#include "dsp/timestretch/time_stretcher.h"

#include "arm_neon.h"

SampleLowLevelReader::SampleLowLevelReader() {
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		clusters[l] = NULL;
	}
}

SampleLowLevelReader::~SampleLowLevelReader() {
	//unassignAllReasons(true); // We would want to call this, but we call it manually instead, cos these often aren't actually destructed
}

void SampleLowLevelReader::unassignAllReasons() {
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		if (clusters[l]) {
			audioFileManager.removeReasonFromCluster(clusters[l], "E027");
			clusters[l] = NULL;
		}
	}
}

// Relative to audio file start, including WAV file header.
// May return negative number - I think particularly if we're going in reversed and just cancelled reading from cache
int32_t SampleLowLevelReader::getPlayByteLowLevel(Sample* sample, SamplePlaybackGuide* guide,
                                                  bool compensateForInterpolationBuffer) {
	if (clusters[0]) {
		uint32_t withinCluster = (uint32_t)currentPlayPos - (uint32_t)&clusters[0]->data + 4
		                         - sample->byteDepth; // Remove deliberate misalignment

		if (compensateForInterpolationBuffer && interpolationBufferSizeLastTime) {
			int extraSamples = -(interpolationBufferSizeLastTime >> 1);
			//if (oscPos >= 8388608) extraSamples++; // This would be good, but we go one better and just copy this to the new hop, in time stretching
			withinCluster += extraSamples * sample->numChannels * sample->byteDepth * guide->playDirection;
		}
		return (clusters[0]->clusterIndex << audioFileManager.clusterSizeMagnitude) + withinCluster;
	}
	else {
		return (int32_t)guide->endPlaybackAtByte
		       + (int32_t)(uint32_t)currentPlayPos
		             * guide->playDirection; // Hopefully this won't go negative, cos we're returning as unsigned...
	}
}

void SampleLowLevelReader::setupForPlayPosMovedIntoNewCluster(SamplePlaybackGuide* guide, Sample* sample,
                                                              int bytePosWithinNewCluster, int byteDepth) {

#if ALPHA_OR_BETA_VERSION
	if (!clusters[0]) numericDriver.freezeWithError("i022");
#endif

	// Ok, now we've just moved the play-pos into a new Cluster, so do some setting up for that
	currentPlayPos = clusters[0]->data + bytePosWithinNewCluster;

	setupReassessmentLocation(guide, sample);
}

void SampleLowLevelReader::misalignPlaybackParameters(Sample* sample) {
	reassessmentLocation = reassessmentLocation - 4 + sample->byteDepth;
	clusterStartLocation = clusterStartLocation - 4 + sample->byteDepth;
	currentPlayPos = currentPlayPos - 4 + sample->byteDepth;
}

void SampleLowLevelReader::realignPlaybackParameters(Sample* sample) {
	reassessmentLocation = reassessmentLocation + 4 - sample->byteDepth;
	currentPlayPos = currentPlayPos + 4 - sample->byteDepth;
}

// Returns false if fail, which can happen if we've actually ended up past the finalClusterIndex cos we were reading cache before.
// There is no guarantee that this won't put the reassessmentLocation back before the currentPlayPos, which is not generally allowed
// (though it'd be harmless for "natively" playing Samples). Caller must ensure safety here.
bool SampleLowLevelReader::reassessReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample,
                                                        int priorityRating) {
	//Uart::println("reassessing");

	if (!clusters[0])
		return true; // Is this for if we've gone past the end of the audio data, while re-pitching / interpolating?

	realignPlaybackParameters(sample);

	int clusterIndex = clusters[0]->clusterIndex;

	// We may have ended up past the finalClusterIndex if we've just switched from using a cache.
	// This needs correcting, so "looping" can occur at next render. Must happen before setupReassessmentLocation() is called.
	int finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers());
	if ((clusterIndex - finalClusterIndex) * guide->playDirection > 0) {
		Uart::println("saving from being past finalCluster");
		Cluster* finalCluster = sample->clusters.getElement(finalClusterIndex)->cluster;
		if (!finalCluster) return false;

		int32_t bytePosWithinCluster = (uint32_t)currentPlayPos - (uint32_t)clusters[0]->data;
		bytePosWithinCluster += (clusterIndex - finalClusterIndex) * audioFileManager.clusterSize;

		currentPlayPos = finalCluster->data + bytePosWithinCluster;
		clusterIndex = finalClusterIndex;
	}

	unassignAllReasons(); // Can only do this after we've done the above stuff, which references clusters, which this will clear
	bool success = assignClusters(guide, sample, clusterIndex, priorityRating);
	if (!success) {
		Uart::println("reassessReassessmentLocation fail");
		return false;
	}
	setupReassessmentLocation(guide, sample);
	return true;
}

// There is no guarantee that this won't put the reassessmentLocation back before the currentPlayPos, which is not generally allowed
// (though it'd be harmless for "natively" playing Samples). Caller must ensure safety here.
// I only discovered this bug / requirement in Sept 2020. Going to assume that only reassessReassessmentLocation() really needs to do this...
void SampleLowLevelReader::setupReassessmentLocation(SamplePlaybackGuide* guide, Sample* sample) {

#if ALPHA_OR_BETA_VERSION
	if (!clusters[0]) numericDriver.freezeWithError("i021");
#endif

	int bytesPerSample = (sample->byteDepth * sample->numChannels);

	int currentClusterIndex = clusters[0]->clusterIndex;

	int32_t endPlaybackAtByte;
	int finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers(), &endPlaybackAtByte);

	// Is this the final Cluster?
	if (currentClusterIndex == finalClusterIndex) {
		int bytePosWithinClusterToStopAt = endPlaybackAtByte & (audioFileManager.clusterSize - 1);
		if (guide->playDirection == 1) {
			if (bytePosWithinClusterToStopAt == 0) bytePosWithinClusterToStopAt = audioFileManager.clusterSize;
		}

		else {
			if (bytePosWithinClusterToStopAt > audioFileManager.clusterSize - bytesPerSample)
				bytePosWithinClusterToStopAt -= audioFileManager.clusterSize;
		}

		reassessmentLocation = &clusters[0]->data[bytePosWithinClusterToStopAt];
		reassessmentAction = REASSESSMENT_ACTION_STOP_OR_LOOP;
	}

	// Or if it's not the final Cluster...
	else {
		reassessmentAction = REASSESSMENT_ACTION_NEXT_CLUSTER;

		// Playing forwards
		if (guide->playDirection == 1) {

			uint32_t bytesBeforeCurrentClusterEnd =
			    (currentClusterIndex + 1) * audioFileManager.clusterSize - sample->audioDataStartPosBytes;
			int excess = bytesBeforeCurrentClusterEnd % (uint8_t)bytesPerSample;
			if (excess == 0) excess = bytesPerSample;
			uint32_t endPosWithinCurrentCluster = audioFileManager.clusterSize + bytesPerSample - excess;

#if ALPHA_OR_BETA_VERSION
			if ((endPosWithinCurrentCluster + currentClusterIndex * audioFileManager.clusterSize
			     - sample->audioDataStartPosBytes)
			    % bytesPerSample)
				numericDriver.freezeWithError("E163");
#endif
			reassessmentLocation = clusters[0]->data + endPosWithinCurrentCluster;
		}

		// Playing backwards
		else {

			uint32_t bytesBeforeCurrentClusterEnd =
			    currentClusterIndex * audioFileManager.clusterSize
			    - sample->audioDataStartPosBytes; // Well, it's really the "start" - the left-most edge
			int excess = bytesBeforeCurrentClusterEnd % (uint8_t)bytesPerSample;
			if (excess == 0) excess = bytesPerSample;

			int32_t endPosWithinCurrentCluster = -excess;
			reassessmentLocation = clusters[0]->data + endPosWithinCurrentCluster;
		}
	}

	// Do the Cluster start location
	// Playing forwards
	if (guide->playDirection == 1) {
		int firstClusterWithData = sample->getFirstClusterIndexWithAudioData();
		if (currentClusterIndex == firstClusterWithData)
			clusterStartLocation =
			    &clusters[0]->data[sample->audioDataStartPosBytes & (audioFileManager.clusterSize - 1)];
		else clusterStartLocation = clusters[0]->data;
	}

	// Playing backwards
	else {
		int audioDataStopPos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
		int highestClusterIndex =
		    audioDataStopPos
		    >> audioFileManager
		           .clusterSizeMagnitude; // There may actually be 1 less Cluster than this if the audio data ends right at the Cluster end, but that won't cause problems
		if (currentClusterIndex == highestClusterIndex)
			clusterStartLocation = &clusters[0]->data[(audioDataStopPos - 1) & (audioFileManager.clusterSize - 1)];
		else clusterStartLocation = &clusters[0]->data[audioFileManager.clusterSize - 1];
	}

	misalignPlaybackParameters(sample);
}

// Make sure reasons are unassigned before you call this!
// Call changeClusterIfNecessary() after this if byteOvershoot isn't 0
bool SampleLowLevelReader::setupClusersForInitialPlay(SamplePlaybackGuide* guide, Sample* sample, int byteOvershoot,
                                                      bool justLooped, int priorityRating) {

	if (sample->unplayable) return false; // TODO: this probably shouldn't be here

	// Assign all the upcoming Clusters...
	uint32_t startPlaybackAtByte = guide->getBytePosToStartPlayback(justLooped);
	startPlaybackAtByte += byteOvershoot * guide->playDirection;

	bool success = setupClustersForPlayFromByte(guide, sample, startPlaybackAtByte, priorityRating);

	if (!success) {
		Uart::println("setupClustersForInitialPlay fail");
	}

	return success;
}

// Make sure reasons are unassigned before you call this!
// Call changeClusterIfNecessary() after this if byteOvershoot isn't 0
bool SampleLowLevelReader::setupClustersForPlayFromByte(SamplePlaybackGuide* guide, Sample* sample,
                                                        int32_t startPlaybackAtByte, int priorityRating) {

	// Change in Aug 2019 - we return false if stuff is out of range. Seems right? Previously we were constraining ClusterIndex
	// to the range, but not changing startPlaybackAtByte - didn't seem to make sense.
	// Or, should it maybe do the "outputting zeros" thing instead? Possibly not too important, as this only gets called
	// for an "initial play", and on time-stretch hop, which goes and will try some alternative stuff if this fails
	if (startPlaybackAtByte < sample->audioDataStartPosBytes
	    || startPlaybackAtByte >= sample->audioDataStartPosBytes + sample->audioDataLengthBytes) {
		return false;
	}

	int clusterIndex = startPlaybackAtByte >> audioFileManager.clusterSizeMagnitude;

	bool success = assignClusters(guide, sample, clusterIndex, priorityRating);
	if (!success) {
		Uart::println("setupClustersForPlayFromByte fail");
		Uart::print("byte: ");
		Uart::println(startPlaybackAtByte);
		return false;
	}

	int32_t bytePosWithinNewCluster = startPlaybackAtByte - clusterIndex * audioFileManager.clusterSize;

	setupForPlayPosMovedIntoNewCluster(guide, sample, bytePosWithinNewCluster, sample->byteDepth);

	// No check has been made that currentPlayPos is not already later than the new reassessmentLocation.
	// If caller isn't sure about this, call changeClustersIfNecessary().
	// changeClustersIfNecessary() itself calls this function when it changes current Cluster, so we absolutely couldn't call it from here.
	return true;
}

// Unassign the old ones before you call this.
bool SampleLowLevelReader::assignClusters(SamplePlaybackGuide* guide, Sample* sample, int clusterIndex,
                                          int priorityRating) {
	int finalClusterIndex = guide->getFinalClusterIndex(sample, shouldObeyMarkers());

	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {

		// Grab it.
		clusters[l] = sample->clusters.getElement(clusterIndex)
		                  ->getCluster(sample, clusterIndex, CLUSTER_ENQUEUE, priorityRating);

		// The first one is required to not only have returned an object to us (which it might not have if insufficient RAM or maybe other reasons),
		// but also to be fully loaded.
		if (l == 0) {

			if (!clusters[l] || !clusters[l]->loaded) {
				return false;
			}
		}

		// If that was the final Cluster, that's all we need to do
		if (clusterIndex == finalClusterIndex) break;

		clusterIndex += guide->playDirection;
	}

	return true;
}

bool SampleLowLevelReader::moveOnToNextCluster(SamplePlaybackGuide* guide, Sample* sample, int priorityRating) {

#if ALPHA_OR_BETA_VERSION
	if (!clusters[0]) numericDriver.freezeWithError("i019");
#endif

	int oldClusterIndex = clusters[0]->clusterIndex;

	int bytePosWithinOldCluster = (uint32_t)currentPlayPos - (uint32_t)&clusters[0]->data;
	audioFileManager.removeReasonFromCluster(clusters[0], "E035");

	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD - 1; l++) {
		clusters[l] = clusters[l + 1];
	}

	clusters[NUM_CLUSTERS_LOADED_AHEAD - 1] = NULL;

	// First things first - if there is no next Cluster or it's not loaded...
	if (!clusters[0]) {
		Uart::print("reached end of waveform. last Cluster was: ");
		Uart::println(oldClusterIndex);
		currentPlayPos = 0;
		return false;
	}

	if (!clusters[0]->loaded) {
		Uart::print("late ");
		Uart::print(clusters[0]->sample->filePath.get());
		Uart::print(" p ");
		Uart::println(clusters[0]->clusterIndex);

		return false;
	}

	// Remove the compensation we'd done on the play pos relating to the byte depth of samples
	bytePosWithinOldCluster = bytePosWithinOldCluster + 4 - sample->byteDepth;

	// And for the one at the far end, just grab the next one
	Cluster* oldLastCluster = clusters[NUM_CLUSTERS_LOADED_AHEAD - 2];

	if (oldLastCluster) {
		int prevClusterIndex = oldLastCluster->clusterIndex;

		int newClusterIndex = prevClusterIndex + guide->playDirection;

		// Check that there actually is a next Cluster. If not...
		if (newClusterIndex * guide->playDirection
		    > guide->getFinalClusterIndex(sample, shouldObeyMarkers()) * guide->playDirection) {
			clusters[NUM_CLUSTERS_LOADED_AHEAD - 1] = NULL;
		}

		// Or if there is...
		else {

			// Grab it.
			clusters[NUM_CLUSTERS_LOADED_AHEAD - 1] =
			    sample->clusters.getElement(newClusterIndex)
			        ->getCluster(sample, newClusterIndex, CLUSTER_ENQUEUE, priorityRating);

			// If that failed (because no free RAM), no damage gets done.
		}
	}

	setupForPlayPosMovedIntoNewCluster(guide, sample,
	                                   bytePosWithinOldCluster - audioFileManager.clusterSize * guide->playDirection,
	                                   sample->byteDepth);

	return true;
}

// Returns false if stopping deliberately or clusters weren't loaded in time. In that case, caller may wish to output some zeros to work through the interpolation buffer.
// All reasons (e.g. clusters[0]) will be unassigned / set to NULL in this case.
bool SampleLowLevelReader::changeClusterIfNecessary(SamplePlaybackGuide* guide, Sample* sample, bool loopingAtLowLevel,
                                                    int priorityRating) {

#if ALPHA_OR_BETA_VERSION
	int count = 0;
#endif

	while (true) {
		int32_t byteOvershoot =
		    (int32_t)((uint32_t)currentPlayPos - (uint32_t)reassessmentLocation) * guide->playDirection;

		if (byteOvershoot < 0) break;

		if (reassessmentAction == REASSESSMENT_ACTION_NEXT_CLUSTER) {
			bool success = moveOnToNextCluster(guide, sample, priorityRating);
			if (!success) {
				Uart::println("next failed");
				return false;
			}
		}
		else { // LOOP_OR_STOP
			unassignAllReasons();
			if (loopingAtLowLevel) {
				bool success = setupClusersForInitialPlay(guide, sample, byteOvershoot, true, priorityRating);
				if (!success) {
					Uart::println("loop failed");
					// TODO: shouldn't we set currentPlayPos = 0 here too?
					return false;
				}
			}
			else {
				currentPlayPos = 0;
				return false;
			}
		}

#if ALPHA_OR_BETA_VERSION
		count++;
		if (count >= 1024)
			numericDriver.freezeWithError(
			    "E227"); // This happened one time! When stopping AudioClips from playing back, after recording and mucking around with SD card reaching full
#endif
	}
	return true;
}

void SampleLowLevelReader::fillInterpolationBufferRetrospectively(Sample* sample, int bufferSize, int startI,
                                                                  int playDirection) {

	// Fill up the furthest-back end of the interpolation buffer
	char* thisPlayPos = currentPlayPos;
	for (int i = startI; i < bufferSize; i++) {

		if (!clusters[0]) {
justWriteZeros:
			interpolationBuffer[0][0][i] = 0;
			if (sample->numChannels == 2) {
				interpolationBuffer[1][0][i] = 0;
			}
		}

		else {

			// Jump back 1 sample
			thisPlayPos = thisPlayPos - playDirection * sample->numChannels * sample->byteDepth;
			int bytesPastClusterStart = ((int)thisPlayPos - (int)clusterStartLocation) * playDirection;

			// If there was valid audio data there...
			if (bytesPastClusterStart >= 0) {
				interpolationBuffer[0][0][i] = *(int16_t*)(thisPlayPos + 2);

				if (sample->numChannels == 2) {
					interpolationBuffer[1][0][i] = *(int16_t*)(thisPlayPos + 2 + sample->byteDepth);
				}
			}

			// Or if not, just write zeros
			else goto justWriteZeros;
		}
	}
}

bool SampleLowLevelReader::fillInterpolationBufferForward(SamplePlaybackGuide* guide, Sample* sample,
                                                          int interpolationBufferSize, bool loopingAtLowLevel,
                                                          int numSpacesToFill, int priorityRating) {
	for (int i = numSpacesToFill - 1; i >= 0; i--) {

		if (!clusters[0]) {
doZeroesFillingBuffer:
			interpolationBuffer[0][0][i] = 0;
			if (sample->numChannels == 2) {
				interpolationBuffer[1][0][i] = 0;
			}
			currentPlayPos++;
			if ((uint32_t)currentPlayPos >= interpolationBufferSize) return false;
		}

		else {

			bool stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
			if (!stillGoing) {
				goto doZeroesFillingBuffer;
			}

			interpolationBuffer[0][0][i] = *(int16_t*)(currentPlayPos + 2);
			if (sample->numChannels == 2) {
				interpolationBuffer[1][0][i] = *(int16_t*)(currentPlayPos + 2 + sample->byteDepth);
			}

			// And move forward one more
			currentPlayPos += sample->numChannels * sample->byteDepth * guide->playDirection;
		}
	}

	return true;
}

void SampleLowLevelReader::jumpBackSamples(Sample* sample, int numToJumpBack, int playDirection) {
	while (numToJumpBack--) { // Could probably be more efficient, but why bother - this whole function is rare

		// Jump back 1 sample
		char* newPlayPos = currentPlayPos - playDirection * sample->numChannels * sample->byteDepth;
		int bytesPastClusterStart = ((int)newPlayPos - (int)clusterStartLocation) * playDirection;

		// If there was no valid audio data there...
		if (bytesPastClusterStart < 0) {
			Uart::println("failed to go back!");
			break;
		}

		// Ok, cool, we can go there
		currentPlayPos = newPlayPos;
	}
}

// This sets up for the reading of some samples. If interpolating, it jumps the play-head forward one output-sample, in place of
// this happening for the first one in the fast rendering routine (it still happens there for all but the first). This is because
// the rendering of the first output-sample may require multiple source-samples to be fed into the interpolation buffer, and
// they might not all be in the same cluster - they might span across a cluster boundary. This function is equipped to deal
// with this, whereas the fast rendering routine is not. This function will be called again whenever any such cluster-changing
// situation arises.
//
// Non-interpolating playback, too, checks in this function (so at the start of the render) whether we need to change to the
// next cluster.
bool SampleLowLevelReader::considerUpcomingWindow(SamplePlaybackGuide* guide, Sample* sample, int* numSamples,
                                                  int32_t phaseIncrement, bool loopingAtLowLevel,
                                                  int interpolationBufferSize, bool allowEndlessSilenceAtEnd,
                                                  int priorityRating) {

	if (ALPHA_OR_BETA_VERSION && phaseIncrement < 0) numericDriver.freezeWithError("E228");

	int bytesPerSample = sample->numChannels * sample->byteDepth;

	// Interpolating
	if (phaseIncrement != 16777216) {

		// But if we weren't interpolating last time...
		if (!interpolationBufferSizeLastTime) {
			interpolationBufferSizeLastTime = interpolationBufferSize;

			int halfBufferSize = interpolationBufferSize >> 1;

			// Fill up the furthest-back end of the interpolation buffer
			fillInterpolationBufferRetrospectively(sample, interpolationBufferSize, halfBufferSize,
			                                       guide->playDirection);

			// And fill up to end of interpolation buffer
			bool success = fillInterpolationBufferForward(guide, sample, interpolationBufferSize, loopingAtLowLevel,
			                                              halfBufferSize, priorityRating);
			if (!success) return false;

			if (ALPHA_OR_BETA_VERSION && clusters[0]) {
				int32_t bytesLeftWhichMayBeRead =
				    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
				if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E222");
			}
		}

		// Or, if interpolation buffer size has changed...
		else if (interpolationBufferSizeLastTime != interpolationBufferSize) {

			// Shrink buffer...
			if (interpolationBufferSize < interpolationBufferSizeLastTime) {

				if (ALPHA_OR_BETA_VERSION && clusters[0]) {
					int32_t bytesLeftWhichMayBeRead =
					    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E305");
				}

				int difference = interpolationBufferSizeLastTime - interpolationBufferSize;
				int offset = difference >> 1;

				for (int i = 0; i < interpolationBufferSize; i++) {
					interpolationBuffer[0][0][i] = interpolationBuffer[0][0][i + offset];
					if (sample->numChannels == 2) {
						interpolationBuffer[1][0][i] = interpolationBuffer[1][0][i + offset];
					}
				}

				jumpBackSamples(sample, offset, guide->playDirection);

				if (ALPHA_OR_BETA_VERSION && clusters[0]) {
					int32_t bytesLeftWhichMayBeRead =
					    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E306");
				}
			}

			// Expand buffer...
			else {

				if (ALPHA_OR_BETA_VERSION && clusters[0]) {
					int32_t bytesLeftWhichMayBeRead =
					    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E308");
				}

				int difference = interpolationBufferSize - interpolationBufferSizeLastTime;
				int offset = difference >> 1;

				for (int i = 0; i < interpolationBufferSizeLastTime; i++) {
					interpolationBuffer[0][0][i + offset] = interpolationBuffer[0][0][i];
					if (sample->numChannels == 2) {
						interpolationBuffer[1][0][i + offset] = interpolationBuffer[1][0][i];
					}
				}

				// And fill up to end of interpolation buffer
				bool success = fillInterpolationBufferForward(guide, sample, interpolationBufferSize, loopingAtLowLevel,
				                                              offset, priorityRating);
				if (!success) return false;

				// If still here, fill far end with zeros. Not perfect, but it'll do.
				for (int i = (interpolationBufferSize - offset); i < interpolationBufferSize; i++) {
					interpolationBuffer[0][0][i] = 0;
					if (sample->numChannels == 2) {
						interpolationBuffer[1][0][i] = 0;
					}
				}

				if (ALPHA_OR_BETA_VERSION && clusters[0]) {
					int32_t bytesLeftWhichMayBeRead =
					    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
					if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E221");
				}
			}

			interpolationBufferSizeLastTime = interpolationBufferSize;
		}

		oscPos += phaseIncrement;
		int numSamplesToJumpForward = oscPos >> 24;

		// If jumping forward at least 1...
		if (numSamplesToJumpForward) {
			oscPos &= 16777215;

			if (clusters[0]) {
				// If by more than INTERPOLATION_BUFFER_SIZE, we need to do a pre-jump to a buffer's-length before we're jumping forward to, to fill up the buffer
				if (numSamplesToJumpForward > interpolationBufferSize) {
					currentPlayPos +=
					    (numSamplesToJumpForward - interpolationBufferSize) * bytesPerSample * guide->playDirection;
					numSamplesToJumpForward = interpolationBufferSize; // That's how much jumping is left to do
				}
			}

			while (numSamplesToJumpForward--) {

				if (!clusters[0]) {
doZeroes:
					bufferZeroForInterpolation(sample->numChannels);
					if (!allowEndlessSilenceAtEnd && (uint32_t)currentPlayPos >= interpolationBufferSize) return false;
				}
				else {

					bool stillGoing = changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating);
					if (!stillGoing) {
						// If we actually just reached the end, go do some zeros
						if (!clusters[0]) goto doZeroes;

						// Otherwise, a Cluster wasn't loaded in time. So just cut the sound
						return false;
					}

					if (ALPHA_OR_BETA_VERSION) {
						if (!clusters[0]) numericDriver.freezeWithError("E225");

						int32_t bytesLeftWhichMayBeRead =
						    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
						if (bytesLeftWhichMayBeRead <= 0) numericDriver.freezeWithError("E226");
					}

					// Grab the value of the sample we're now at, to use for interpolation
					bufferIndividualSampleForInterpolation(sample->bitMask, sample->numChannels, sample->byteDepth,
					                                       currentPlayPos);

					// And move forward one more
					currentPlayPos += bytesPerSample * guide->playDirection;

					if (ALPHA_OR_BETA_VERSION) {
						int32_t bytesLeftWhichMayBeRead =
						    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
						if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E185");
					}
				}
			}
		}

		// Or if not jumping forward any samples...
		else {
			if (ALPHA_OR_BETA_VERSION && clusters[0]) {

				// That should mean we've already read this one, so we definitely shouldn't be beyond the reassessmentLocation...
				int32_t bytesLeftWhichMayBeRead =
				    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
				if (bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E223");
			}
		}

		// currentPlayPos may now actually be at or beyond the reassessmentLocation - that's ok
		// Wait, I can see how it could be at it, but surely it couldn't be beyond it anymore?

		// The rest of this window is going to require that we jump forward (*numSamples - 1) times
		if (*numSamples >= 2) {

			int samplesWeWantToReadThisWindow = ((uint64_t)phaseIncrement * (*numSamples - 1) + oscPos) >> 24;

			unsigned int samplesLeftWhichMayBeRead;
			bool shouldShorten;

			// If finished waveform and just reading zeros
			if (!clusters[0]) {
				if (allowEndlessSilenceAtEnd) return true;
				samplesLeftWhichMayBeRead = interpolationBufferSize - (uint32_t)currentPlayPos;
				shouldShorten = (samplesWeWantToReadThisWindow > samplesLeftWhichMayBeRead);
			}

			// Or if still going on waveform
			else {
				int32_t bytesLeftWhichMayBeRead =
				    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;
				if (ALPHA_OR_BETA_VERSION && bytesLeftWhichMayBeRead < 0) numericDriver.freezeWithError("E148");

				int bytesWeWantToRead = samplesWeWantToReadThisWindow * bytesPerSample;
				shouldShorten = (bytesWeWantToRead > bytesLeftWhichMayBeRead);
				if (shouldShorten) {
					samplesLeftWhichMayBeRead =
					    (uint16_t)bytesLeftWhichMayBeRead
					    / (uint8_t)bytesPerSample; // If we're here, we know bytesLeftWhichMayBeRead is quite small
				}
			}

			// If there aren't actually enough samples / bytes left...
			if (shouldShorten) {

				int64_t phaseIncrementingLeftWhichMayBeDone =
				    ((uint64_t)(samplesLeftWhichMayBeRead + 1) << 24) - oscPos - 1;

				// This really really should never happen.
				if (ALPHA_OR_BETA_VERSION && phaseIncrementingLeftWhichMayBeDone < 0) {
					if (!clusters[0]) numericDriver.freezeWithError("E143");
					else numericDriver.freezeWithError("E000");
				}

				uint32_t numPhaseIncrementsLeftWhichMayBeDone =
				    (uint64_t)phaseIncrementingLeftWhichMayBeDone / (uint32_t)phaseIncrement;

				// We add 1 because remember, we were just considering (numSamples - 1) the whole time - because we've already done a jump-forward for the first sample to read
				*numSamples = numPhaseIncrementsLeftWhichMayBeDone + 1;
			}
		}
	}

	// No interpolating
	else {

		// But if we were interpolating last time...
		if (interpolationBufferSizeLastTime) {

			if (!clusters[0]) return false;

			int numToJumpBack = (interpolationBufferSizeLastTime >> 1) - (oscPos >> 23);
			jumpBackSamples(sample, numToJumpBack, guide->playDirection);
			interpolationBufferSizeLastTime = 0;

			oscPos = 0;
		}

		// Check if we already ended up at the end of the Cluster after the last window
		if (!changeClusterIfNecessary(guide, sample, loopingAtLowLevel, priorityRating)) {
			return false;
		}

		// If the end is coming in this window, deal with it
		int32_t bytesLeftWhichMayBeRead =
		    (int32_t)((uint32_t)reassessmentLocation - (uint32_t)currentPlayPos) * guide->playDirection;

		if (ALPHA_OR_BETA_VERSION && bytesLeftWhichMayBeRead <= 0) {
			numericDriver.freezeWithError("E001");
		}

		// If there are actually less bytes remaining than we ideally wanted for this window...
		if (*numSamples * bytesPerSample > bytesLeftWhichMayBeRead) {
			*numSamples = (uint32_t)bytesLeftWhichMayBeRead / (uint8_t)bytesPerSample;

			if (ALPHA_OR_BETA_VERSION && *numSamples <= 0) {
				Uart::print("bytesLeftWhichMayBeRead: ");
				Uart::println(bytesLeftWhichMayBeRead);
				numericDriver.freezeWithError(
				    "E147"); // Crazily, Michael B got in Nov 2022, when "closing" a recorded loop.
			}
		}
	}

	return true;
}

void SampleLowLevelReader::bufferIndividualSampleForInterpolation(uint32_t bitMask, int numChannels, int byteDepth,
                                                                  char* __restrict__ playPosNow) {

	// This works better than using memmoves. Ideally we'd switch this off if not smoothly interpolating - check that that's actually more efficient though
	for (int i = INTERPOLATION_MAX_NUM_SAMPLES - 1; i >= 1; i--) {
		interpolationBuffer[0][0][i] = interpolationBuffer[0][0][i - 1];
		if (numChannels == 2) {
			interpolationBuffer[1][0][i] = interpolationBuffer[1][0][i - 1];
		}
	}

	interpolationBuffer[0][0][0] = *(int16_t*)(playPosNow + 2);

	if (numChannels == 2) {
		interpolationBuffer[1][0][0] = *(int16_t*)(playPosNow + 2 + byteDepth);
	}
}

void SampleLowLevelReader::bufferZeroForInterpolation(int numChannels) {

	// This works better than using memmoves. Ideally we'd switch this off if not smoothly interpolating - check that that's actually more efficient though
	for (int i = INTERPOLATION_MAX_NUM_SAMPLES - 1; i >= 1; i--) {
		interpolationBuffer[0][0][i] = interpolationBuffer[0][0][i - 1];
		if (numChannels == 2) {
			interpolationBuffer[1][0][i] = interpolationBuffer[1][0][i - 1];
		}
	}

	interpolationBuffer[0][0][0] = 0;

	if (numChannels == 2) {
		interpolationBuffer[1][0][0] = 0;
	}

	currentPlayPos++;
}

// This could be optimized, but why bother, it doesn't get called much
void SampleLowLevelReader::jumpForwardZeroes(int bufferSize, int numChannels, int32_t phaseIncrement) {

	oscPos += phaseIncrement;
	int numSamplesToJumpForward = oscPos >> 24;
	if (numSamplesToJumpForward) {
		oscPos &= 16777215;

		while (numSamplesToJumpForward--) {
			// Grab the value of the sample we're now at, to use for interpolation
			bufferZeroForInterpolation(numChannels);
		}
	}
}

void SampleLowLevelReader::jumpForwardLinear(int numChannels, int byteDepth, uint32_t bitMask, int jumpAmount,
                                             int32_t phaseIncrement) {

	oscPos += phaseIncrement;
	int numSamplesToJumpForward = oscPos >> 24;
	if (numSamplesToJumpForward) {
		oscPos &= 16777215;

		// If jumping forward by more than INTERPOLATION_BUFFER_SIZE, we first need to jump to the one before we're jumping forward to, to grab its value
		if (numSamplesToJumpForward > 2) {
			currentPlayPos += (numSamplesToJumpForward - 2) * jumpAmount;
			//numSamplesToJumpForward = 2; // Not necessasry
		}

		if (numChannels == 2) {
			if (numSamplesToJumpForward >= 2) {
				interpolationBuffer[0][0][1] = *(int16_t*)(currentPlayPos + 2);
				interpolationBuffer[1][0][1] = *(int16_t*)(currentPlayPos + 2 + byteDepth);
				currentPlayPos += jumpAmount;
			}
			else {
				interpolationBuffer[0][0][1] = interpolationBuffer[0][0][0];
				interpolationBuffer[1][0][1] = interpolationBuffer[1][0][0];
			}
			interpolationBuffer[1][0][0] = *(int16_t*)(currentPlayPos + 2 + byteDepth);
		}

		else {
			if (numSamplesToJumpForward >= 2) {
				interpolationBuffer[0][0][1] = *(int16_t*)(currentPlayPos + 2);
				currentPlayPos += jumpAmount;
			}
			else {
				interpolationBuffer[0][0][1] = interpolationBuffer[0][0][0];
			}
		}

		// Putting these down here did speed things up!
		interpolationBuffer[0][0][0] = *(int16_t*)(currentPlayPos + 2);
		currentPlayPos += jumpAmount;
	}
}

#define numBitsInTableSize 8
#define rshiftAmount                                                                                                   \
	((24 + INTERPOLATION_MAX_NUM_SAMPLES_MAGNITUDE) - 16 - numBitsInTableSize                                          \
	 + 1) // that's (numBitsInInput - 16 - numBitsInTableSize); = 4 for now

void SampleLowLevelReader::interpolate(int32_t* __restrict__ sampleRead, int numChannelsNow, int whichKernel) {
#include "dsp/interpolation/interpolate.h"
}

void SampleLowLevelReader::interpolateLinear(int32_t* __restrict__ sampleRead, int numChannelsNow, int whichKernel) {
#include "dsp/interpolation/interpolate_linear.h"
}

// This stuff is in its own function here rather than in Voice because for some reason it's faster
void SampleLowLevelReader::readSamplesResampled(int32_t** __restrict__ oscBufferPos, int numSamplesTotal,
                                                Sample* sample, int jumpAmount, int numChannels,
                                                int numChannelsAfterCondensing, int32_t phaseIncrement,
                                                int32_t* amplitude, int32_t amplitudeIncrement,
                                                int interpolationBufferSize, bool writingCache,
                                                char** __restrict__ cacheWritePos, bool* __restrict__ doneAnySamplesYet,
                                                TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
                                                int whichKernel) {

	uint32_t const bitMask = sample->bitMask;
	int const byteDepth = sample->byteDepth;

	int32_t* __restrict__ oscBufferPosNow = *oscBufferPos;

	char* __restrict__ cacheWritePosNow = (char*)*cacheWritePos;

	int32_t const* const oscBufferEnd = oscBufferPosNow + numSamplesTotal * numChannelsAfterCondensing;

	bool const stillGotActualData = clusters[0];

	// Windowed sinc interpolation
	if (interpolationBufferSize > 2) {

		char* __restrict__ currentPlayPosNow = currentPlayPos + 2;

		if (!*doneAnySamplesYet) {
			*doneAnySamplesYet = true;
			goto skipFirstSmooth;
		}

		do {

			if (__builtin_expect(stillGotActualData, 1)) {

				oscPos += phaseIncrement;
				int numSamplesToJumpForward = oscPos >> 24;
				if (numSamplesToJumpForward) {
					oscPos &= 16777215;

					// If jumping forward by more than INTERPOLATION_MAX_NUM_SAMPLES, we first need to jump to the one before we're jumping forward to, to grab its value
					if (numSamplesToJumpForward > INTERPOLATION_MAX_NUM_SAMPLES) {
						currentPlayPosNow += (numSamplesToJumpForward - INTERPOLATION_MAX_NUM_SAMPLES) * jumpAmount;
						numSamplesToJumpForward = INTERPOLATION_MAX_NUM_SAMPLES;
					}

					int16_t sourceL = *(int16_t*)currentPlayPosNow;

					for (int i = INTERPOLATION_MAX_NUM_SAMPLES - 1; i >= numSamplesToJumpForward; i--) {
						interpolationBuffer[0][0][i] = interpolationBuffer[0][0][i - numSamplesToJumpForward];
					}

					if (numChannels == 2) {
						for (int i = INTERPOLATION_MAX_NUM_SAMPLES - 1; i >= numSamplesToJumpForward; i--) {
							interpolationBuffer[1][0][i] = interpolationBuffer[1][0][i - numSamplesToJumpForward];
						}

						numSamplesToJumpForward--;

						while (true) {
							interpolationBuffer[0][0][numSamplesToJumpForward] = sourceL;
							interpolationBuffer[1][0][numSamplesToJumpForward] =
							    *(int16_t*)(currentPlayPosNow + byteDepth);
							currentPlayPosNow += jumpAmount;
							if (!numSamplesToJumpForward) goto skipFirstSmooth;
							numSamplesToJumpForward--;
							sourceL = *(int16_t*)currentPlayPosNow;
						}
					}

					else {

						numSamplesToJumpForward--;

						while (true) {
							currentPlayPosNow += jumpAmount;
							interpolationBuffer[0][0][numSamplesToJumpForward] = sourceL;
							if (!numSamplesToJumpForward) goto skipFirstSmooth;
							sourceL = *(int16_t*)currentPlayPosNow;
							numSamplesToJumpForward--;
						}
					}
				}
			}
			else jumpForwardZeroes(interpolationBufferSize, numChannels, phaseIncrement);

skipFirstSmooth:
			int32_t sampleRead[2];
			interpolate(sampleRead, numChannels, whichKernel);

			int32_t existingValueL = *oscBufferPosNow;

			// If caching, do that now
			if (writingCache) {
				for (int i = 4 - CACHE_BYTE_DEPTH; i < 4; i++) {
					*cacheWritePosNow = ((char*)&sampleRead[0])[i];
					cacheWritePosNow++;
				}

				if (numChannels == 2) {
					for (int i = 4 - CACHE_BYTE_DEPTH; i < 4; i++) {
						*cacheWritePosNow = ((char*)&sampleRead[1])[i];
						cacheWritePosNow++;
					}
				}
			}

			// If condensing to mono, do that now
			if (numChannels == 2 && numChannelsAfterCondensing == 1) {
				sampleRead[0] = ((sampleRead[0] >> 1) + (sampleRead[1] >> 1));
			}

			*amplitude += amplitudeIncrement;

			// Mono / left channel (or stereo condensed to mono)
			*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(
			    existingValueL, sampleRead[0],
			    *amplitude); // sourceAmplitude is modified above; using accumulate made no difference
			oscBufferPosNow++;

			// Right channel
			if (numChannelsAfterCondensing == 2) {
				int32_t existingValueR = *oscBufferPosNow;
				*oscBufferPosNow =
				    multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead[1], *amplitude);
				oscBufferPosNow++;
			}
		} while (oscBufferPosNow != oscBufferEnd);

		currentPlayPos = currentPlayPosNow - 2;
	}

	// Linear interpolation
	else {
		if (!*doneAnySamplesYet) {
			*doneAnySamplesYet = true;
			goto skipFirstLinear;
		}

		do {
			if (stillGotActualData) jumpForwardLinear(numChannels, byteDepth, bitMask, jumpAmount, phaseIncrement);
			else jumpForwardZeroes(interpolationBufferSize, numChannels, phaseIncrement);

skipFirstLinear:
			int32_t sampleRead[2];
			interpolateLinear(sampleRead, numChannels, whichKernel);

			int32_t existingValueL = *oscBufferPosNow;

			// If condensing to mono, do that now
			if (numChannels == 2 && numChannelsAfterCondensing == 1) {
				sampleRead[0] = ((sampleRead[0] >> 1) + (sampleRead[1] >> 1));
			}

			*amplitude += amplitudeIncrement;

			// Mono / left channel (or stereo condensed to mono)
			*oscBufferPosNow = multiply_accumulate_32x32_rshift32_rounded(
			    existingValueL, sampleRead[0],
			    *amplitude); // sourceAmplitude is modified above; using accumulate made no difference
			oscBufferPosNow++;

			// Right channel
			if (numChannelsAfterCondensing == 2) {
				int32_t existingValueR = *oscBufferPosNow;
				*oscBufferPosNow =
				    multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleRead[1], *amplitude);
				oscBufferPosNow++;
			}
		} while (oscBufferPosNow != oscBufferEnd);
	}

	*oscBufferPos = oscBufferPosNow;
	if (cacheWritePos) *cacheWritePos = cacheWritePosNow;
}

void SampleLowLevelReader::readSamplesNative(int32_t** __restrict__ bufferPos, int numSamplesTotal, Sample* sample,
                                             int jumpAmount, int numChannels, int numChannelsAfterCondensing,
                                             int32_t* __restrict__ amplitude, int32_t amplitudeIncrement,
                                             TimeStretcher* timeStretcher, bool bufferingToTimeStretcher) {

	char* __restrict__ currentPlayPosNow = currentPlayPos;
	int32_t* __restrict__ bufferPosNow = *bufferPos;
	int32_t const* const bufferEndNow = bufferPosNow + numSamplesTotal * numChannelsAfterCondensing;

	int const byteDepth = sample->byteDepth;
	uint32_t const bitMask = sample->bitMask;

	do {
		int32_t sampleReadL = *(int32_t*)currentPlayPosNow;

		int32_t existingValueL = *bufferPosNow;
		*amplitude += amplitudeIncrement;
		sampleReadL &= bitMask;

		int32_t sampleReadR;
		if (numChannels == 2) {
			sampleReadR = *(int32_t*)(currentPlayPosNow + byteDepth) & bitMask;

			// If condensing to mono, do that now
			if (numChannelsAfterCondensing == 1) {
				sampleReadL = ((sampleReadL >> 1) + (sampleReadR >> 1));
			}
		}

		currentPlayPosNow += jumpAmount;

		// Mono / left channel (or stereo condensed to mono)
		*bufferPosNow = multiply_accumulate_32x32_rshift32_rounded(
		    existingValueL, sampleReadL,
		    *amplitude); // *amplitude is modified above; using accumulate made no difference
		bufferPosNow++;

		// Right channel
		if (numChannelsAfterCondensing == 2) {
			int32_t existingValueR = *bufferPosNow;
			*bufferPosNow = multiply_accumulate_32x32_rshift32_rounded(existingValueR, sampleReadR, *amplitude);
			bufferPosNow++;
		}
	} while (bufferPosNow != bufferEndNow);

	*bufferPos = bufferPosNow;
	currentPlayPos = currentPlayPosNow;
}

// Returns false if actual error. Not if it just reached the end. In that case it just sets timeStretcher->playHeadStillActive[whichPlayHead] to false
bool SampleLowLevelReader::readSamplesForTimeStretching(int32_t* outputBuffer, SamplePlaybackGuide* guide,
                                                        Sample* sample, int numSamples, int numChannels,
                                                        int numChannelsAfterCondensing, int32_t phaseIncrement,
                                                        int32_t amplitude, int32_t amplitudeIncrement,
                                                        bool loopingAtLowLevel, int jumpAmount, int bufferSize,
                                                        TimeStretcher* timeStretcher, bool bufferingToTimeStretcher,
                                                        int whichPlayHead, int whichKernel, int priorityRating) {

	do {
		int samplesNow = numSamples;

		timeStretcher->playHeadStillActive[whichPlayHead] = considerUpcomingWindow(
		    guide, sample, &samplesNow, phaseIncrement, loopingAtLowLevel, bufferSize, 0, priorityRating);
		if (!timeStretcher->playHeadStillActive[whichPlayHead]) {

			// If we got false, that can just mean end of waveform. But if clusters[0] has been set to NULL too, that means (SD card) error
			if (clusters[0]) return false;

			//Uart::println("one head no longer active for timeStretcher");
			break;
		}

		// No resampling
		if (phaseIncrement == 16777216) {
			readSamplesNative(&outputBuffer, samplesNow, sample, jumpAmount, numChannels, numChannelsAfterCondensing,
			                  &amplitude, amplitudeIncrement, timeStretcher, bufferingToTimeStretcher);
		}

		// Resampling
		else {
			bool doneAnySamplesYet = false;
			readSamplesResampled(&outputBuffer, samplesNow, sample, jumpAmount, numChannels, numChannelsAfterCondensing,
			                     phaseIncrement, &amplitude, amplitudeIncrement, bufferSize, false, NULL,
			                     &doneAnySamplesYet, timeStretcher, bufferingToTimeStretcher, whichKernel);
		}

		numSamples -= samplesNow;
	} while (numSamples);

	return true;
}

void SampleLowLevelReader::cloneFrom(SampleLowLevelReader* other, bool stealReasons) {

	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		if (clusters[l]) {
			audioFileManager.removeReasonFromCluster(clusters[l], "E131");
		}

		clusters[l] = other->clusters[l];

		if (clusters[l]) {
			if (stealReasons) {
				other->clusters[l] = NULL;
			}
			else {
				audioFileManager.addReasonToCluster(clusters[l]);
			}
		}
	}

	memcpy(interpolationBuffer, other->interpolationBuffer, sizeof(interpolationBuffer));

	oscPos = other->oscPos;
	currentPlayPos = other->currentPlayPos;
	reassessmentLocation = other->reassessmentLocation;
	clusterStartLocation = other->clusterStartLocation;
	reassessmentAction = other->reassessmentAction;
	interpolationBufferSizeLastTime = other->interpolationBufferSizeLastTime;
}
