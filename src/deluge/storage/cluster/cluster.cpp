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

#include "storage/cluster/cluster.h"
#include "definitions_cxx.hpp"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "util/misc.h"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>

// The universal size of all clusters
size_t Cluster::size = 32768;
size_t Cluster::size_magnitude = 15;

void Cluster::setSize(size_t size) {
	Cluster::size = size;

	// Find the highest bit set
	Cluster::size_magnitude = 32 - std::countl_zero(size) - 1;
}

Cluster* Cluster::create(Cluster::Type type, bool shouldAddReasons, void* dontStealFromThing) {
	audioFileManager.setCardRead(); // even if it hasn't been we're now commited to the cluster size
	void* memory = GeneralMemoryAllocator::get().allocStealable(sizeof(Cluster) + Cluster::size, dontStealFromThing);
	if (memory == nullptr) {
		return nullptr;
	}

	auto* cluster = new (memory) Cluster();

	cluster->type = type;

	if (shouldAddReasons) {
		cluster->addReason();
	}

	return cluster;
}

void Cluster::destroy() {
	this->~Cluster(); // Removes reasons, and / or from stealable list
	delugeDealloc(this);
}

/**
 * @brief This function goes through the contents of the cluster,
 *        and converts them to the Deluge's native PCM 24-bit format if needed
 */
void Cluster::convertDataIfNecessary() {
	// We haven't yet figured out where the audio data starts
	if (sample->audioDataStartPosBytes == 0) {
		return;
	}

	if (sample->rawDataFormat != RawDataFormat::NATIVE) {
		std::copy(data, &data[3], firstThreeBytesPreDataConversion);

		int32_t startPos = sample->audioDataStartPosBytes;
		int32_t startCluster = startPos >> Cluster::size_magnitude;

		if (clusterIndex < startCluster) { // Hmm, there must have been a case where this happens...
			return;
		}

		// Special case for 24-bit with its uneven number of bytes
		if (sample->rawDataFormat == RawDataFormat::ENDIANNESS_WRONG_24) {
			char* pos;

			if (clusterIndex == startCluster) {
				pos = &data[startPos & (Cluster::size - 1)];
			}
			else {
				uint32_t bytesBeforeStartOfCluster = clusterIndex * Cluster::size - sample->audioDataStartPosBytes;
				int32_t bytesThatWillBeEatingIntoAnother3Byte = bytesBeforeStartOfCluster % 3;
				if (bytesThatWillBeEatingIntoAnother3Byte == 0) {
					bytesThatWillBeEatingIntoAnother3Byte = 3;
				}
				pos = &data[3 - bytesThatWillBeEatingIntoAnother3Byte];
			}

			char const* endPos;
			if (clusterIndex == sample->getFirstClusterIndexWithNoAudioData() - 1) {
				uint32_t endAtBytePos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
				uint32_t endAtPosWithinCluster = endAtBytePos & (Cluster::size - 1);
				endPos = &data[endAtPosWithinCluster];
			}
			else {
				endPos = &data[Cluster::size - 2];
			}

			while (true) {
				char const* endPosNow = pos + 1024; // Every this many bytes, we'll pause and do an audio routine
				endPosNow = std::min(endPosNow, endPos);

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
				// Sean: keeping AudioEngine::routine() call here instead of YieldToAudio to be safe.
				// Sean: replace AudioEngine::routine() call with call to run AudioEngine::routine() task
				runTask(AudioEngine::routine_task_id);
			}
		}

		// Or, all other bit depths
		else {
			int32_t* pos;

			if (clusterIndex == startCluster) {
				pos = (int32_t*)&data[startPos & (Cluster::size - 1)];
			}
			else {
				pos = (int32_t*)&data[startPos & 0b11];
			}

			int32_t* endPos;
			if (clusterIndex == sample->getFirstClusterIndexWithNoAudioData() - 1) {
				uint32_t endAtBytePos = sample->audioDataStartPosBytes + sample->audioDataLengthBytes;
				uint32_t endAtPosWithinCluster = endAtBytePos & (Cluster::size - 1);
				endPos = (int32_t*)&data[endAtPosWithinCluster];
			}
			else {
				endPos = (int32_t*)&data[Cluster::size - 3];
			}

			for (; pos < endPos; pos++) {

				if (!((uint32_t)pos & 0b1111111100)) {
					// Sean: keeping AudioEngine::routine() call here instead of YieldToAudio to be safe.
					// Sean: replace AudioEngine::routine() call with call to run AudioEngine::routine() task
					runTask(AudioEngine::routine_task_id);
				}

				*pos = sample->convertToNative(*pos);
			}
		}
	}
}

StealableQueue Cluster::getAppropriateQueue() const {
	StealableQueue q;

	// If it's a perc cache...
	if (type == Type::PERC_CACHE_FORWARDS || type == Type::PERC_CACHE_REVERSED) {
		q = sample->numReasonsToBeLoaded ? StealableQueue::CURRENT_SONG_SAMPLE_DATA_PERC_CACHE
		                                 : StealableQueue::NO_SONG_SAMPLE_DATA_PERC_CACHE;
	}

	// If it's a regular repitched cache...
	else if (sampleCache) {
		q = (sampleCache->sample->numReasonsToBeLoaded) ? StealableQueue::CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE
		                                                : StealableQueue::NO_SONG_SAMPLE_DATA_REPITCHED_CACHE;
	}

	// Or, if it has a Sample...
	else if (sample) {
		q = sample->numReasonsToBeLoaded ? StealableQueue::CURRENT_SONG_SAMPLE_DATA
		                                 : StealableQueue::NO_SONG_SAMPLE_DATA;

		if (sample->rawDataFormat != RawDataFormat::NATIVE) {
			q = static_cast<StealableQueue>(util::to_underlying(q) + 1); // next queue
		}
	}

	return q;
}

void Cluster::steal(char const* errorCode) {

	// Ok, we're now gonna decide what to do according to the actual "type" field for this Cluster.
	switch (type) {

	case Type::SAMPLE:
		if (ALPHA_OR_BETA_VERSION && sample == nullptr) {
			FREEZE_WITH_ERROR("E181");
		}
		sample->clusters.getElement(clusterIndex)->cluster = nullptr;
		break;

	case Type::SAMPLE_CACHE:
		if (ALPHA_OR_BETA_VERSION && sampleCache == nullptr) {
			FREEZE_WITH_ERROR("E183");
		}
		sampleCache->clusterStolen(clusterIndex);

		// If first Cluster, delete whole cache. Wait, no, something might still be pointing to the cache...
		/*
		if (!clusterIndex) {
		    sampleCache->sample->deleteCache(sampleCache);
		}
		*/
		break;

	case Type::PERC_CACHE_FORWARDS:
	case Type::PERC_CACHE_REVERSED:
		if (ALPHA_OR_BETA_VERSION && sample == nullptr) {
			FREEZE_WITH_ERROR("E184");
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
	case Type::SAMPLE_CACHE:
		return (sampleCache != thingNotToStealFrom);

	case Type::PERC_CACHE_FORWARDS:
	case Type::PERC_CACHE_REVERSED:
		return (sample != thingNotToStealFrom);

	// explicit fallthrough cases
	case Type::SAMPLE:
	case Type::EMPTY:
	case Type::GENERAL_MEMORY:
	case Type::OTHER:;
	}
	return true;
}

void Cluster::addReason() {
	// If it's going to cease to be zero, it's become unavailable,
	// so remove it from the stealables queue
	if (this->numReasonsToBeLoaded == 0) {
		this->unlink(); // Remove from stealables queue
	}

	this->numReasonsToBeLoaded++;
}
