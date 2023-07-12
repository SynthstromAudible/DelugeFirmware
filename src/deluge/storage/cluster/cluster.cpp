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

#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "io/uart/uart.h"
#include "model/sample/sample.h"
#include "util/functions.h"
#include <string.h>
#include "model/sample/sample_cache.h"
#include "hid/display/numeric_driver.h"

Cluster::Cluster() {
	sample = NULL;
	clusterIndex = 0;
	sampleCache = NULL;
	extraBytesAtStartConverted = false;
	extraBytesAtEndConverted = false;
	loaded = false;
	numReasonsHeldBySampleRecorder = 0;
	numReasonsToBeLoaded = 0;
	// type is not set here, set it yourself (can't remember exact reason...)
}

void Cluster::convertDataIfNecessary() {

	if (!sample->audioDataStartPosBytes) {
		return; // Or maybe we haven't yet figured out where the audio data starts
	}

	if (sample->rawDataFormat) {

		memcpy(firstThreeBytesPreDataConversion, data, 3);

		int startPos = sample->audioDataStartPosBytes;
		int startCluster = startPos >> audioFileManager.clusterSizeMagnitude;

		if (clusterIndex < startCluster) { // Hmm, there must have been a case where this happens...
			return;
		}

		// Special case for 24-bit with its uneven number of bytes
		if (sample->rawDataFormat == RAW_DATA_ENDIANNESS_WRONG_24) {
			char* pos;

			if (clusterIndex == startCluster) {
				pos = &data[startPos & (audioFileManager.clusterSize - 1)];
			}
			else {
				uint32_t bytesBeforeStartOfCluster =
				    clusterIndex * audioFileManager.clusterSize - sample->audioDataStartPosBytes;
				int bytesThatWillBeEatingIntoAnother3Byte = bytesBeforeStartOfCluster % 3;
				if (bytesThatWillBeEatingIntoAnother3Byte == 0) {
					bytesThatWillBeEatingIntoAnother3Byte = 3;
				}
				pos = &data[3 - bytesThatWillBeEatingIntoAnother3Byte];
			}

			char const* endPos;
			if (clusterIndex == sample->getFirstClusterIndexWithNoAudioData() - 1) {
				uint32_t endAtBytePos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
				uint32_t endAtPosWithinCluster = endAtBytePos & (audioFileManager.clusterSize - 1);
				endPos = &data[endAtPosWithinCluster];
			}
			else {
				endPos = &data[audioFileManager.clusterSize - 2];
			}

			while (true) {
				char const* endPosNow = pos + 1024; // Every this many bytes, we'll pause and do an audio routine
				if (endPosNow > endPos) {
					endPosNow = endPos;
				}

				while (pos < endPosNow) {
					uint8_t temp = pos[0];
					pos[0] = pos[2];
					pos[2] = temp;
					pos += 3;
				}

				if (pos >= endPos) {
					break;
				}

				AudioEngine::logAction("from convert-data");
				AudioEngine::routine(); // ----------------------------------------------------
			}
		}

		// Or, all other bit depths
		else {
			int32_t* pos;

			if (clusterIndex == startCluster) {
				pos = (int32_t*)&data[startPos & (audioFileManager.clusterSize - 1)];
			}
			else {
				pos = (int32_t*)&data[startPos & 0b11];
			}

			int32_t* endPos;
			if (clusterIndex == sample->getFirstClusterIndexWithNoAudioData() - 1) {
				uint32_t endAtBytePos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
				uint32_t endAtPosWithinCluster = endAtBytePos & (audioFileManager.clusterSize - 1);
				endPos = (int32_t*)&data[endAtPosWithinCluster];
			}
			else {
				endPos = (int32_t*)&data[audioFileManager.clusterSize - 3];
			}

			//uint16_t startTime = MTU2.TCNT_0;

			for (; pos < endPos; pos++) {

				if (!((uint32_t)pos & 0b1111111100)) {
					AudioEngine::logAction("from convert-data");
					AudioEngine::routine(); // ----------------------------------------------------
				}

				sample->convertOneData(pos);
			}

			/*
			uint16_t endTime = MTU2.TCNT_0;

			if (clusterIndex != startCluster) {
				Uart::print("time to convert: ");
				Uart::println((uint16_t)(endTime - startTime));
			}
			*/
		}
	}
}

int Cluster::getAppropriateQueue() {
	int q;

	// If it's a perc cache...
	if (type == CLUSTER_PERC_CACHE_FORWARDS || type == CLUSTER_PERC_CACHE_REVERSED) {
		q = sample->numReasonsToBeLoaded ? STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_PERC_CACHE
		                                 : STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_PERC_CACHE;
	}

	// If it's a regular repitched cache...
	else if (sampleCache) {
		q = (sampleCache->sample->numReasonsToBeLoaded) ? STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE
		                                                : STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA_REPITCHED_CACHE;
	}

	// Or, if it has a Sample...
	else if (sample) {
		q = sample->numReasonsToBeLoaded ? STEALABLE_QUEUE_CURRENT_SONG_SAMPLE_DATA
		                                 : STEALABLE_QUEUE_NO_SONG_SAMPLE_DATA;

		if (sample->rawDataFormat) {
			q++;
		}
	}

	return q;
}

void Cluster::steal(char const* errorCode) {

	// Ok, we're now gonna decide what to do according to the actual "type" field for this Cluster.
	switch (type) {

	case CLUSTER_SAMPLE:
		if (ALPHA_OR_BETA_VERSION && !sample) {
			numericDriver.freezeWithError("E181");
		}
		sample->clusters.getElement(clusterIndex)->cluster = NULL;
		break;

	case CLUSTER_SAMPLE_CACHE:
		if (ALPHA_OR_BETA_VERSION && !sampleCache) {
			numericDriver.freezeWithError("E183");
		}
		sampleCache->clusterStolen(clusterIndex);

		// If first Cluster, delete whole cache. Wait, no, something might still be pointing to the cache...
		/*
		if (!clusterIndex) {
			sampleCache->sample->deleteCache(sampleCache);
		}
		*/
		break;

	case CLUSTER_PERC_CACHE_FORWARDS:
	case CLUSTER_PERC_CACHE_REVERSED:
		if (ALPHA_OR_BETA_VERSION && !sample) {
			numericDriver.freezeWithError("E184");
		}
		sample->percCacheClusterStolen(this);
		break;

	default: // Otherwise, nothing needs to happen
		break;
	}
}

bool Cluster::mayBeStolen(void* thingNotToStealFrom) {
	if (numReasonsToBeLoaded) {
		return false;
	}

	if (!thingNotToStealFrom) {
		return true;
	}

	switch (type) {
	case CLUSTER_SAMPLE_CACHE:
		return (sampleCache != thingNotToStealFrom);

	case CLUSTER_PERC_CACHE_FORWARDS:
	case CLUSTER_PERC_CACHE_REVERSED:
		return (sample != thingNotToStealFrom);
	}
	return true;
}
