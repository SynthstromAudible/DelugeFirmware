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
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_cache.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "util/misc.h"

#include "deluge_resource.h" // resource manager: manager-owned clusters lease instead of queueing
#include <algorithm>
#include <cstddef>
#include <cstring>

// The universal size of all clusters
size_t Cluster::size = 32768;
size_t Cluster::size_magnitude = 15;

void Cluster::setSize(size_t size) {
	Cluster::size = size;

	// Find the highest bit set
	Cluster::size_magnitude = 32 - __builtin_clz(size) - 1;
}

void Cluster::destroy() {
	this->~Cluster();
	// Release back to the slab so its table entry is cleared (a bare deluge_free /
	// delugeDealloc would leave a dangling slot pointing at freed memory).
	GeneralMemoryAllocator::get().freeSdram(this);
}

// Safety net (see the header): release through the slab so the table entry is cleared.
// freeSdram() falls back to a plain heap free for any non-slab pointer.
void Cluster::operator delete(void* ptr) {
	GeneralMemoryAllocator::get().freeSdram(ptr);
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
				AudioEngine::runRoutine();
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
					AudioEngine::runRoutine();
				}

				*pos = sample->convertToNative(*pos);
			}
		}
	}
}

// The resource-manager Asset that owns this cluster's residency for the *leased* (reason-tracked)
// kinds — SAMPLE (the sample's asset) and PERC_CACHE_* (the sample's per-direction perc asset).
// SAMPLE_CACHE clusters are unleased (never reasoned), so they return NO_ASSET here and are managed
// via their cache's own Asset instead.
uint32_t Cluster::resourceLeaseAssetId() const {
	switch (type) {
	case Type::SAMPLE:
		return (sample != nullptr) ? sample->resourceAssetId : DELUGE_RESOURCE_NO_ASSET;
	case Type::PERC_CACHE_FORWARDS:
		return (sample != nullptr) ? sample->percCacheAssetId[0] : DELUGE_RESOURCE_NO_ASSET;
	case Type::PERC_CACHE_REVERSED:
		return (sample != nullptr) ? sample->percCacheAssetId[1] : DELUGE_RESOURCE_NO_ASSET;
	default:
		return DELUGE_RESOURCE_NO_ASSET; // SAMPLE_CACHE is unleased
	}
}

void Cluster::addReason() {
	// Manager-owned leased clusters (SAMPLE / PERC) are pinned by a resource-manager lease (they're
	// never on a stealable queue). Take a lease so the manager won't evict a cluster the caller still
	// holds, and keep numReasonsToBeLoaded as a mirror.
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr != nullptr) {
		deluge_resource_add_lease(mgr, this); // hit-only lease bump on this resident chunk
	}
	this->numReasonsToBeLoaded++;
}
