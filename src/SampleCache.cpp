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

#include <Cluster.h>
#include "SampleCache.h"
#include "SampleManager.h"
#include "uart.h"
#include "numericdriver.h"
#include "Sample.h"
#include "GeneralMemoryAllocator.h"

SampleCache::SampleCache(Sample* newSample, int newNumChunks, int newWaveformLengthBytes, int newPhaseIncrement, int newTimeStretchRatio, int newSkipSamplesAtStart) {
	sample = newSample;
	phaseIncrement = newPhaseIncrement;
	timeStretchRatio = newTimeStretchRatio;
	writeBytePos = 0;
#if ALPHA_OR_BETA_VERSION
	numChunks = newNumChunks;
#endif
	waveformLengthBytes = newWaveformLengthBytes;
	skipSamplesAtStart = newSkipSamplesAtStart;
	/*
	for (int i = 0; i < numChunks; i++) {
		loadedSampleChunks[i] = NULL; // We don't actually have to initialize these, since writeBytePos tells us how many are "valid"
	}
	*/
}

SampleCache::~SampleCache()
{
	unlinkChunks(0, true);
}


void SampleCache::chunkStolen(int chunkIndex) {

#if ALPHA_OR_BETA_VERSION
	if (chunkIndex < 0) numericDriver.freezeWithError("E296");
	else if (chunkIndex >= numChunks) numericDriver.freezeWithError("E297");
#endif

	Uart::println("cache chunk stolen");

	// There's now no point in having any further chunks
	unlinkChunks(chunkIndex + 1, false); // Must do this before changing writeBytePos

	uint8_t bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;

	writeBytePos = (uint32_t)((uint32_t)((chunkIndex << sampleManager.clusterSizeMagnitude) + bytesPerSample - 1) / bytesPerSample) * bytesPerSample; // Make it a multiple of bytesPerSample - but round up.
																																// If you try and simplify this, make sure it still works for 0 and doesn't go negative or anything!

#if ALPHA_OR_BETA_VERSION
	if (writeBytePos < 0) numericDriver.freezeWithError("E298");
	else if (writeBytePos >= waveformLengthBytes) numericDriver.freezeWithError("E299");

	int numExistentChunks = getNumExistentChunks(writeBytePos);

	if (numExistentChunks != chunkIndex) {
		numericDriver.freezeWithError("E295");
	}
	loadedSampleChunks[chunkIndex] = NULL; // No need to remove this first chunk from a queue or anything - that's already all done by the thing that's stealing it
#endif
}

void SampleCache::unlinkChunks(int startAtIndex, bool beingDestructed) {
	// And there's now no point in having any further chunks
	int numExistentChunks = getNumExistentChunks(writeBytePos);
	for (int i = startAtIndex; i < numExistentChunks; i++) {
		if (ALPHA_OR_BETA_VERSION && !loadedSampleChunks[i]) numericDriver.freezeWithError("E167");

		sampleManager.deallocateLoadedSampleChunk(loadedSampleChunks[i]);

		if (ALPHA_OR_BETA_VERSION && !beingDestructed) loadedSampleChunks[i] = NULL;
	}
}

// You must be sure before calling this that newWriteBytePos is a multiple of (sample->numChannels * CACHE_BYTE_DEPTH)
void SampleCache::setWriteBytePos(int newWriteBytePos) {

#if ALPHA_OR_BETA_VERSION
	if (newWriteBytePos < 0) numericDriver.freezeWithError("E300");
	if (newWriteBytePos > waveformLengthBytes) numericDriver.freezeWithError("E301");

	uint32_t bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;
	if (newWriteBytePos != (uint32_t)newWriteBytePos / bytesPerSample * bytesPerSample) numericDriver.freezeWithError("E302");
#endif

	// When setting it earlier, we may have to discard some chunks.
	// Remember, a cache cluster actually gets (bytesPerSample - 1) extra usable bytes after it.

	int newNumExistentChunks = getNumExistentChunks(newWriteBytePos);
	unlinkChunks(newNumExistentChunks, false);

	writeBytePos = newWriteBytePos;

	if (ALPHA_OR_BETA_VERSION && getNumExistentChunks(writeBytePos) != newNumExistentChunks) numericDriver.freezeWithError("E294");
}



// Does not move the new chunk to the appropriate "availability queue", because it's expected that the caller is just about to call getChunk(), to get it,
// which will call prioritizeNotStealingChunk(), and that'll do it
bool SampleCache::setupNewChunk(int chunkIndex) {
	//Uart::println("writing cache to new chunk");

#if ALPHA_OR_BETA_VERSION
	if (chunkIndex >= numChunks) numericDriver.freezeWithError("E126");
	if (chunkIndex > getNumExistentChunks(writeBytePos)) numericDriver.freezeWithError("E293");
#endif

	loadedSampleChunks[chunkIndex] = sampleManager.allocateLoadedSampleChunk(LOADED_SAMPLE_CHUNK_SAMPLE_CACHE, false, this); // Do not add reasons, and don't steal from this SampleCache
	if (!loadedSampleChunks[chunkIndex]) { // If that allocation failed...
		Uart::println("allocation fail");
		return false;
	}

	loadedSampleChunks[chunkIndex]->chunkIndex = chunkIndex;
	loadedSampleChunks[chunkIndex]->sampleCache = this;

	return true;
}



void SampleCache::prioritizeNotStealingChunk(int chunkIndex) {

	if (generalMemoryAllocator.getRegion(loadedSampleChunks[chunkIndex]) != MEMORY_REGION_SDRAM) return; // Sorta just have to do this

	// This ensures, one chunk at a time, that this Cache's chunks are right at the far end of their queue (so won't be stolen for a while),
	// but in reverse order so that the later-in-sample of those cache chunks will be stolen first

	// Remember, cache clusters never have "reasons", so we can assume these are already in one of the stealableClusterQueues, ready to be "stolen".

	// First chunk
	if (chunkIndex == 0) {
		if (loadedSampleChunks[chunkIndex]->list != &generalMemoryAllocator.regions[MEMORY_REGION_SDRAM].stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE]
				|| !loadedSampleChunks[chunkIndex]->isLast()) {

			loadedSampleChunks[chunkIndex]->remove(); // Remove from old list, if it was already in one (might not have been).
			generalMemoryAllocator.regions[MEMORY_REGION_SDRAM].stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE].addToEnd(loadedSampleChunks[chunkIndex]);
			generalMemoryAllocator.regions[MEMORY_REGION_SDRAM].stealableClusterQueueLongestRuns[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE] = 0xFFFFFFFF; // TODO: make good.
		}
	}

	// Later chunks
	else {

		if (generalMemoryAllocator.getRegion(loadedSampleChunks[chunkIndex - 1]) != MEMORY_REGION_SDRAM) return; // Sorta just have to do this

		// In most cases, we'll want to do this thing to alter the ordering - including if the chunk in question hasn't actually been added to a queue at all yet,
		// because this functions serves the additional purpose of being what puts chunks in their queue in the first place.
		if (loadedSampleChunks[chunkIndex]->list != &generalMemoryAllocator.regions[MEMORY_REGION_SDRAM].stealableClusterQueues[STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE]
				||
				loadedSampleChunks[chunkIndex]->next != loadedSampleChunks[chunkIndex - 1]) {

			loadedSampleChunks[chunkIndex]->remove(); // Remove from old list, if it was already in one (might not have been).
			loadedSampleChunks[chunkIndex - 1]->insertOtherNodeBefore(loadedSampleChunks[chunkIndex]);
			// TODO: invalidate longest run length on new queue?
		}
	}
}


Cluster* SampleCache::getChunk(int chunkIndex) {
	prioritizeNotStealingChunk(chunkIndex);
	return loadedSampleChunks[chunkIndex];
}


int SampleCache::getNumExistentChunks(int32_t thisWriteBytePos) {
	int bytesPerSample = sample->numChannels * CACHE_BYTE_DEPTH;

	// Remember, a cache cluster actually gets (bytesPerSample - 1) extra usable bytes after it.
	int numExistentChunks = (thisWriteBytePos + sampleManager.clusterSize - bytesPerSample) >> sampleManager.clusterSizeMagnitude;

#if ALPHA_OR_BETA_VERSION
	if (numExistentChunks < 0) numericDriver.freezeWithError("E303");
	if (numExistentChunks > numChunks) numericDriver.freezeWithError("E304");
#endif

	return numExistentChunks;
}
