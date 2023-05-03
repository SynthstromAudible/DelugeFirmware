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

#include <Cluster.h>
#include <SampleCluster.h>
#include "SampleManager.h"
#include "uart.h"
#include "functions.h"
#include "Sample.h"
#include "numericdriver.h"
#include "GeneralMemoryAllocator.h"

SampleCluster::SampleCluster()
{
	loadedSampleChunk = NULL;

	investigatedWholeLength = false;
	minValue = 127;
	maxValue = -128;
	sdAddress = 0; // 0 means invalid, and we check for this as a last resort before writing
}


SampleCluster::~SampleCluster() {
	if (loadedSampleChunk) {

#if ALPHA_OR_BETA_VERSION
		int numReasonsToBeLoaded = loadedSampleChunk->numReasonsToBeLoaded;
		if (loadedSampleChunk == sampleManager.loadedSampleChunkBeingLoaded) numReasonsToBeLoaded --;

		if (numReasonsToBeLoaded) {
			Uart::print("uh oh, some reasons left... ");
			Uart::println(numReasonsToBeLoaded);

			numericDriver.freezeWithError("E036");
		}
#endif
		sampleManager.deallocateLoadedSampleChunk(loadedSampleChunk);
	}
}

void SampleCluster::ensureNoReason(Sample* sample) {
	if (loadedSampleChunk) {
		if (loadedSampleChunk->numReasonsToBeLoaded) {
			Uart::print("chunk has reason! ");
			Uart::println(loadedSampleChunk->numReasonsToBeLoaded);
			Uart::println(sample->filePath.get());

			if (loadedSampleChunk->numReasonsToBeLoaded >= 0)
				numericDriver.freezeWithError("E068");
			else
				numericDriver.freezeWithError("E069");
			delayMS(50);

		}
	}
}

// Calling this will add a reason to the loaded chunk!
// priorityRating is only relevant if enqueuing.
Cluster* SampleCluster::getLoadedSampleChunk(Sample* sample, uint32_t chunkIndex, int loadInstruction, uint32_t priorityRating, uint8_t* error) {

	if (error) *error = NO_ERROR;

	// If the Cluster hasn't been created yet
	if (!loadedSampleChunk) {

		// If the file can no longer be found on the card, we're in trouble
		if (sample->unloadable) {
			Uart::println("unloadable");
			if (error) *error = ERROR_FILE_NOT_FOUND;
			return NULL;
		}

		//Uart::println("loading");
		loadedSampleChunk = sampleManager.allocateLoadedSampleChunk(); // Adds 1 reason

		if (!loadedSampleChunk) {
			Uart::println("couldn't allocate");
			if (error) *error = ERROR_INSUFFICIENT_RAM;
			return NULL;
		}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
		if (loadedSampleChunk->numReasonsToBeLoaded < 1) numericDriver.freezeWithError("i005"); // Diversifying Qui's E341. It should actually be exactly 1
		if (loadedSampleChunk->type != LOADED_SAMPLE_CHUNK_SAMPLE) numericDriver.freezeWithError("E256"); // Cos I got E236
#endif

		loadedSampleChunk->sample = sample;
		loadedSampleChunk->chunkIndex = chunkIndex;

		// Sometimes we don't actually want to load at all - if we're re-processing a WAV file and want to overwrite a whole chunk
		if (loadInstruction == CHUNK_DONT_LOAD) return loadedSampleChunk;

		// If loading later...
		if (loadInstruction == CHUNK_ENQUEUE) {
justEnqueue:

			if (ALPHA_OR_BETA_VERSION && loadedSampleChunk->type != LOADED_SAMPLE_CHUNK_SAMPLE) numericDriver.freezeWithError("E236"); // Cos Chris F got an E205

			sampleManager.enqueueLoadedSampleChunk(loadedSampleChunk, priorityRating); // TODO: If that fails, it'll just get awkwardly forgotten about
#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (loadedSampleChunk && loadedSampleChunk->numReasonsToBeLoaded <= 0) numericDriver.freezeWithError("i027"); // Diversifying Ron R's i004, which was diversifying Qui's E341
#endif
		}

		// Or if want to try to load now...
		else { // CHUNK_LOAD_IMMEDIATELY or CHUNK_LOAD_IMMEDIATELY_OR_ENQUEUE

			// loadedSampleChunk has (at least?) one reason - added above

			if (ALPHA_OR_BETA_VERSION && loadedSampleChunk->type != LOADED_SAMPLE_CHUNK_SAMPLE) numericDriver.freezeWithError("E234"); // Cos Chris F got an E205
			bool result = sampleManager.loadSampleChunk(loadedSampleChunk, 1);

			// If that didn't work...
			if (!result) {

				// If also an acceptable option, then just enqueue it, and we'll keep the "reason" and return the pointer
				if (loadInstruction == CHUNK_LOAD_IMMEDIATELY_OR_ENQUEUE) goto justEnqueue;

				// Or if it was a must-load-now...
				// Free and remove our link to the unloaded Cluster - otherwise the next time we try to load it, it'd still exist but never get enqueued for loading
				sampleManager.deallocateLoadedSampleChunk(loadedSampleChunk); // This removes the 1 reason that it'd still have

				if (error) *error = ERROR_UNSPECIFIED; // TODO: get actual error. Although sometimes it'd just be a "can't do it now cos card's being accessed, and that's fine, thanks for checking."
				loadedSampleChunk = NULL;
			}
#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (loadedSampleChunk && loadedSampleChunk->numReasonsToBeLoaded <= 0) numericDriver.freezeWithError("i026"); // Michael B got - insane.
#endif
		}
	}

	// Or if it had previously been created...
	else {

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
		if (loadedSampleChunk && loadedSampleChunk->numReasonsToBeLoaded < 0) numericDriver.freezeWithError("i028"); // Diversifying Ron R's i004, which was diversifying Qui's E341
#endif

		// If they'd prefer it loaded immediately and it's not loaded, try speeding loading along
		if ((loadInstruction == CHUNK_LOAD_IMMEDIATELY || loadInstruction == CHUNK_LOAD_IMMEDIATELY_OR_ENQUEUE) && !loadedSampleChunk->loaded) {
			sampleManager.loadAnyEnqueuedSampleChunks();

			// If it's still not loaded and it was a must-load-now...
			if (loadInstruction == CHUNK_LOAD_IMMEDIATELY && !loadedSampleChunk->loaded) {
				Uart::print("hurrying loading along failed for index: ");
				Uart::println(chunkIndex);
				if (error) *error = ERROR_UNSPECIFIED; // TODO: get actual error
				return NULL;
			}
		}

		sampleManager.addReasonToLoadedSampleChunk(loadedSampleChunk);

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
		if (loadedSampleChunk && loadedSampleChunk->numReasonsToBeLoaded <= 0) numericDriver.freezeWithError("i025"); // Diversifying Ron R's i004, which was diversifying Qui's E341
#endif
	}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
	if (loadedSampleChunk && loadedSampleChunk->numReasonsToBeLoaded <= 0) numericDriver.freezeWithError("i004"); // Diversifying Qui's E341
#endif

	return loadedSampleChunk;
}
