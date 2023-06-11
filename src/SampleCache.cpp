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

#include <AudioFileManager.h>
#include <Cluster.h>
#include "SampleCache.h"
#include "uart.h"
#include "numericdriver.h"
#include "Sample.h"
#include "GeneralMemoryAllocator.h"

SampleCache::SampleCache(Sample* newSample, int newNumClusters, int newWaveformLengthBytes, int newPhaseIncrement,
                         int newTimeStretchRatio, int newSkipSamplesAtStart) {
	sample = newSample;
	phaseIncrement = newPhaseIncrement;
	timeStretchRatio = newTimeStretchRatio;
	writeBytePos = 0;
#if ALPHA_OR_BETA_VERSION
	numClusters = newNumClusters;
#endif
	waveformLengthBytes = newWaveformLengthBytes;
	skipSamplesAtStart = newSkipSamplesAtStart;
	/*
	for (int i = 0; i < numClusters; i++) {
		clusters[i] = NULL; // We don't actually have to initialize these, since writeBytePos tells us how many are "valid"
	}
	*/
}

SampleCache::~SampleCache() {
	unlinkClusters(0, true);
}

void SampleCache::clusterStolen(int clusterIndex) {

#if ALPHA_OR_BETA_VERSION
	if (clusterIndex < 0) numericDriver.freezeWithError("E296");
	else if (clusterIndex >= numClusters) numericDriver.freezeWithError("E297");
#endif

	Uart::println("cache Cluster stolen");

	// There's now no point in having any further Clusters
	unlinkClusters(clusterIndex + 1, false); // Must do this before changing writeBytePos

	uint8_t bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;

	// Make it a multiple of bytesPerSample - but round up.
	// If you try and simplify this, make sure it still works for 0 and doesn't go negative or anything!
	writeBytePos = (uint32_t)((uint32_t)((clusterIndex << audioFileManager.clusterSizeMagnitude) + bytesPerSample - 1)
	                          / bytesPerSample)
	               * bytesPerSample;

#if ALPHA_OR_BETA_VERSION
	if (writeBytePos < 0) numericDriver.freezeWithError("E298");
	else if (writeBytePos >= waveformLengthBytes) numericDriver.freezeWithError("E299");

	int numExistentClusters = getNumExistentClusters(writeBytePos);

	if (numExistentClusters != clusterIndex) {
		numericDriver.freezeWithError("E295");
	}
	clusters[clusterIndex] =
	    NULL; // No need to remove this first Cluster from a queue or anything - that's already all done by the thing that's stealing it
#endif
}

void SampleCache::unlinkClusters(int startAtIndex, bool beingDestructed) {
	// And there's now no point in having any further Clusters
	int numExistentClusters = getNumExistentClusters(writeBytePos);
	for (int i = startAtIndex; i < numExistentClusters; i++) {
		if (ALPHA_OR_BETA_VERSION && !clusters[i]) numericDriver.freezeWithError("E167");

		audioFileManager.deallocateCluster(clusters[i]);

		if (ALPHA_OR_BETA_VERSION && !beingDestructed) clusters[i] = NULL;
	}
}

// You must be sure before calling this that newWriteBytePos is a multiple of (sample->numChannels * CACHE_BYTE_DEPTH)
void SampleCache::setWriteBytePos(int newWriteBytePos) {

#if ALPHA_OR_BETA_VERSION
	if (newWriteBytePos < 0) numericDriver.freezeWithError("E300");
	if (newWriteBytePos > waveformLengthBytes) numericDriver.freezeWithError("E301");

	uint32_t bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;
	if (newWriteBytePos != (uint32_t)newWriteBytePos / bytesPerSample * bytesPerSample)
		numericDriver.freezeWithError("E302");
#endif

	// When setting it earlier, we may have to discard some Clusters.
	// Remember, a cache cluster actually gets (bytesPerSample - 1) extra usable bytes after it.

	int newNumExistentClusters = getNumExistentClusters(newWriteBytePos);
	unlinkClusters(newNumExistentClusters, false);

	writeBytePos = newWriteBytePos;

	if (ALPHA_OR_BETA_VERSION && getNumExistentClusters(writeBytePos) != newNumExistentClusters)
		numericDriver.freezeWithError("E294");
}

// Does not move the new Cluster to the appropriate "availability queue", because it's expected that the caller is just about to call getCluster(), to get it,
// which will call prioritizeNotStealingCluster(), and that'll do it
bool SampleCache::setupNewCluster(int clusterIndex) {
	//Uart::println("writing cache to new Cluster");

#if ALPHA_OR_BETA_VERSION
	if (clusterIndex >= numClusters) numericDriver.freezeWithError("E126");
	if (clusterIndex > getNumExistentClusters(writeBytePos)) numericDriver.freezeWithError("E293");
#endif

	clusters[clusterIndex] = audioFileManager.allocateCluster(
	    CLUSTER_SAMPLE_CACHE, false, this); // Do not add reasons, and don't steal from this SampleCache
	if (!clusters[clusterIndex]) {          // If that allocation failed...
		Uart::println("allocation fail");
		return false;
	}

	clusters[clusterIndex]->clusterIndex = clusterIndex;
	clusters[clusterIndex]->sampleCache = this;

	return true;
}

void SampleCache::prioritizeNotStealingCluster(int clusterIndex) {

	if (generalMemoryAllocator.getRegion(clusters[clusterIndex]) != MEMORY_REGION_SDRAM)
		return; // Sorta just have to do this

	// This ensures, one Cluster at a time, that this Cache's Clusters are right at the far end of their queue (so won't be stolen for a while),
	// but in reverse order so that the later-in-sample of those cache Clusters will be stolen first

	// Remember, cache clusters never have "reasons", so we can assume these are already in one of the stealableClusterQueues, ready to be "stolen".

	// First Cluster
	if (clusterIndex == 0) {
		if (clusters[clusterIndex]->list
		        != &generalMemoryAllocator.regions[MEMORY_REGION_SDRAM]
		                .stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE]
		    || !clusters[clusterIndex]->isLast()) {

			clusters[clusterIndex]->remove(); // Remove from old list, if it was already in one (might not have been).
			generalMemoryAllocator.regions[MEMORY_REGION_SDRAM]
			    .stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE]
			    .addToEnd(clusters[clusterIndex]);
			generalMemoryAllocator.regions[MEMORY_REGION_SDRAM]
			    .stealableClusterQueueLongestRuns[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE] =
			    0xFFFFFFFF; // TODO: make good.
		}
	}

	// Later Clusters
	else {

		if (generalMemoryAllocator.getRegion(clusters[clusterIndex - 1]) != MEMORY_REGION_SDRAM)
			return; // Sorta just have to do this

		// In most cases, we'll want to do this thing to alter the ordering - including if the Cluster in question hasn't actually been added to a queue at all yet,
		// because this functions serves the additional purpose of being what puts Clusters in their queue in the first place.
		if (clusters[clusterIndex]->list
		        != &generalMemoryAllocator.regions[MEMORY_REGION_SDRAM]
		                .stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE]
		    || clusters[clusterIndex]->next != clusters[clusterIndex - 1]) {

			clusters[clusterIndex]->remove(); // Remove from old list, if it was already in one (might not have been).
			clusters[clusterIndex - 1]->insertOtherNodeBefore(clusters[clusterIndex]);
			// TODO: invalidate longest run length on new queue?
		}
	}
}

Cluster* SampleCache::getCluster(int clusterIndex) {
	prioritizeNotStealingCluster(clusterIndex);
	return clusters[clusterIndex];
}

int SampleCache::getNumExistentClusters(int32_t thisWriteBytePos) {
	int bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;

	// Remember, a cache Cluster actually gets (bytesPerSample - 1) extra usable bytes after it.
	int numExistentClusters =
	    (thisWriteBytePos + audioFileManager.clusterSize - bytesPerSample) >> audioFileManager.clusterSizeMagnitude;

#if ALPHA_OR_BETA_VERSION
	if (numExistentClusters < 0) numericDriver.freezeWithError("E303");
	if (numExistentClusters > numClusters) numericDriver.freezeWithError("E304");
#endif

	return numExistentClusters;
}
