/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <AudioFileManager.h>
#include <Cluster.h>
#include <samplebrowser.h>
#include <SampleHolder.h>
#include "Sample.h"
#include "numericdriver.h"
#include "functions.h"
#include "uart.h"

SampleHolder::SampleHolder() {
	startPos = 0;
	endPos = 9999999;
	waveformViewZoom = 0;
	audioFileType = AUDIO_FILE_TYPE_SAMPLE;

	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		clustersForStart[l] = NULL;
	}
}

SampleHolder::~SampleHolder() {

	// Don't call setSample() - that does writing to variables which isn't necessary

	if ((Sample*)audioFile) {
		unassignAllClusterReasons(true);
#if ALPHA_OR_BETA_VERSION
		if (audioFile->numReasonsToBeLoaded <= 0)
			numericDriver.freezeWithError("E219"); // I put this here to try and catch an E004 Luc got
#endif
		audioFile->removeReason("E396");
	}
}

void SampleHolder::beenClonedFrom(SampleHolder* other, bool reversed) {
	filePath.set(&other->filePath);
	if (other->audioFile) setAudioFile(other->audioFile, reversed);

	startPos = other->startPos;
	endPos = other->endPos;
	waveformViewScroll = other->waveformViewScroll;
	waveformViewZoom = other->waveformViewZoom;
}

void SampleHolder::unassignAllClusterReasons(bool beingDestructed) {
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		if (clustersForStart[l]) {
			audioFileManager.removeReasonFromCluster(clustersForStart[l], "E123");
			if (!beingDestructed) clustersForStart[l] = NULL;
		}
	}
}

int64_t SampleHolder::getEndPos(bool forTimeStretching) {
	if (forTimeStretching) return endPos;
	else return getMin(endPos, ((Sample*)audioFile)->lengthInSamples);
}

int64_t SampleHolder::getDurationInSamples(bool forTimeStretching) {
	return getEndPos(forTimeStretching) - startPos;
}

int32_t SampleHolder::getLengthInSamplesAtSystemSampleRate(bool forTimeStretching) {
	uint64_t lengthInSamples = getDurationInSamples(forTimeStretching);
	if (neutralPhaseIncrement == 16777216) return lengthInSamples;
	else return (lengthInSamples << 24) / neutralPhaseIncrement;
}

void SampleHolder::setAudioFile(AudioFile* newSample, bool reversed, bool manuallySelected,
                                int clusterLoadInstruction) {

	AudioFileHolder::setAudioFile(newSample, reversed, manuallySelected, clusterLoadInstruction);

	if (audioFile) {

		if (manuallySelected && ((Sample*)audioFile)->tempFilePathForRecording.isEmpty())
			sampleBrowser.lastFilePathLoaded.set(&filePath);

		uint32_t lengthInSamples = ((Sample*)audioFile)->lengthInSamples;

		// If we're here as a result of the user having manually selected a new file, set the zone to its actual length.
		if (manuallySelected) {
			startPos = 0;
			endPos = lengthInSamples;
		}

		// Otherwise, simply make sure that the zone doesn't exceed the length of the sample
		else {
			startPos = getMin(startPos, lengthInSamples);
			if (endPos == 0 || endPos == 9999999) endPos = lengthInSamples;
			if (endPos <= startPos) startPos = 0;
		}

		sampleBeenSet(reversed, manuallySelected);

#if 1 || ALPHA_OR_BETA_VERSION
		if (!audioFile) numericDriver.freezeWithError("i031"); // Trying to narrow down E368 that Kevin F got
#endif

		claimClusterReasons(reversed, clusterLoadInstruction);
	}
}

#define MARKER_SAMPLES_BEFORE_TO_CLAIM 150

// Reassesses which Clusters we want to be a "reason" for.
// Ensure there is a sample before you call this.
void SampleHolder::claimClusterReasons(bool reversed, int clusterLoadInstruction) {

	if (ALPHA_OR_BETA_VERSION && !audioFile) numericDriver.freezeWithError("E368");

	//unassignAllReasons(); // This now happens as part of reassessPosForMarker(), called below

	int playDirection = reversed ? -1 : 1;
	int bytesPerSample = audioFile->numChannels * ((Sample*)audioFile)->byteDepth;

	// This code basically copied from VoiceSource::setupPlaybackBounds()
	int startPlaybackAtSample;

	if (!reversed) {
		startPlaybackAtSample = (int64_t)startPos - MARKER_SAMPLES_BEFORE_TO_CLAIM;
		if (startPlaybackAtSample < 0) startPlaybackAtSample = 0;
	}
	else {
		startPlaybackAtSample = getEndPos() - 1 + MARKER_SAMPLES_BEFORE_TO_CLAIM;
		if (startPlaybackAtSample > ((Sample*)audioFile)->lengthInSamples - 1)
			startPlaybackAtSample = ((Sample*)audioFile)->lengthInSamples - 1;
	}

	int startPlaybackAtByte = ((Sample*)audioFile)->audioDataStartPosBytes + startPlaybackAtSample * bytesPerSample;

	claimClusterReasonsForMarker(clustersForStart, startPlaybackAtByte, playDirection, clusterLoadInstruction);
}

void SampleHolder::claimClusterReasonsForMarker(Cluster** clusters, uint32_t startPlaybackAtByte, int playDirection,
                                                int clusterLoadInstruction) {

	int clusterIndex = startPlaybackAtByte >> audioFileManager.clusterSizeMagnitude;

	uint32_t posWithinCluster = startPlaybackAtByte & (audioFileManager.clusterSize - 1);

	// Set up new temp list
	Cluster* newClusters[NUM_CLUSTERS_LOADED_AHEAD];
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		newClusters[l] = NULL;
	}

	// Populate new list
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {

		/*
		// If final one, only load it if posWithinCluster is at least a quarter of the way in
		if (l == NUM_SAMPLE_CLUSTERS_LOADED_AHEAD - 1) {
			if (playDirection == 1) {
				if (posWithinCluster < (sampleManager.clusterSize >> 2)) break;
			}
			else {
				if (posWithinCluster > sampleManager.clusterSize - (sampleManager.clusterSize >> 2)) break;
			}
		}
		*/

		SampleCluster* sampleCluster = ((Sample*)audioFile)->clusters.getElement(clusterIndex);

		newClusters[l] = sampleCluster->getCluster(((Sample*)audioFile), clusterIndex, clusterLoadInstruction);

		if (!newClusters[l]) Uart::println("NULL!!");
		else if (clusterLoadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE && !newClusters[l]->loaded)
			Uart::println("not loaded!!");

		clusterIndex += playDirection;
		if (clusterIndex < ((Sample*)audioFile)->getFirstClusterIndexWithAudioData()
		    || clusterIndex >= ((Sample*)audioFile)->getFirstClusterIndexWithNoAudioData())
			break;
	}

	// Replace old list
	for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
		if (clusters[l]) {
			audioFileManager.removeReasonFromCluster(clusters[l], "E146");
		}
		clusters[l] = newClusters[l];
	}
}
