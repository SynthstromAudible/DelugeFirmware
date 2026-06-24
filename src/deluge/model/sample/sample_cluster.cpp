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

#include "model/sample/sample_cluster.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include <cstddef>

#include "deluge_resource.h" // resource manager: route raw SAMPLE-cluster residency through it

SampleCluster::~SampleCluster() {
	if (cluster) {

#if ALPHA_OR_BETA_VERSION
		uint32_t reasons = cluster->leaseCount();
		if (cluster == audioFileManager.clusterBeingLoaded && reasons > 0) {
			reasons--;
		}

		if (reasons) {
			D_PRINTLN("uh oh, some reasons left...  %d", reasons);

			// Bay_Mud got this, and thinks a FlashAir card might have been a catalyst. It still "shouldn't" be able to
			// happen though.
			FREEZE_WITH_ERROR("E036");
		}
#endif
		cluster->destroy();
	}
}

void SampleCluster::ensureNoReason(Sample* sample) {
	if (cluster) {
		if (cluster->leaseCount()) {
			D_PRINTLN("Cluster has reason!  %d %d", cluster->leaseCount(), sample->filePath.c_str());
			FREEZE_WITH_ERROR("E068");
			delayMS(50);
		}
	}
}

// Calling this will add a reason to the loaded Cluster!
// priorityRating is only relevant if enqueuing.
Cluster* SampleCluster::getCluster(Sample* sample, uint32_t clusterIndex, int32_t loadInstruction,
                                   uint32_t priorityRating, Error* error) {

	if (error != nullptr) {
		*error = Error::NONE;
	}

	// Manager-owned residency. The manager is the sole SDRAM evictor: every Sample (playback or
	// recording) is manager-owned (ensureResourceAsset FREEZEs if the asset table is exhausted — no
	// legacy fallback). The hard-lease count lives in the manager's chunk slot (the construct/
	// materialize callback records the slot handle); add_lease/request take the lease. non-null
	// `cluster` <=> manager-resident (on_evict nulls it).
	uint32_t asset = sample->ensureResourceAsset();
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	bool wasResident = (cluster != nullptr);

	if (loadInstruction == CLUSTER_DONT_LOAD) {
		// "Allocate but don't read from the card" — recording / convert write target. Resident ⇒ just
		// pin (lease); not-resident ⇒ construct an empty cluster (no I/O). Held *dirty* so the manager
		// never evicts the unflushed data; writeCluster clears dirty once it is on the card, after
		// which it is reconstructable like any sample cluster.
		if (wasResident) {
			deluge_resource_add_lease(mgr, cluster);
		}
		else {
			void* p = deluge_resource_request(mgr, asset, clusterIndex, sizeof(Cluster) + Cluster::size);
			if (p == nullptr) {
				if (error != nullptr) {
					*error = sample->unloadable ? Error::FILE_NOT_FOUND : Error::INSUFFICIENT_RAM;
				}
				return nullptr;
			}
			cluster = reinterpret_cast<Cluster*>(p);
		}
		deluge_resource_mark_dirty(mgr, cluster, true);
		return cluster;
	}

	if (loadInstruction == CLUSTER_ENQUEUE) {
		// Async prefetch: construct + lease now (NO I/O), then schedule the read on the loader
		// (the existing loadingQueue, pumped off the audio thread) so the audio thread never
		// blocks on SD. Returns the cluster (loaded==false until the loader reads it).
		void* p = deluge_resource_request(mgr, asset, clusterIndex, sizeof(Cluster) + Cluster::size);
		if (p == nullptr) {
			if (error != nullptr) {
				*error = sample->unloadable ? Error::FILE_NOT_FOUND : Error::INSUFFICIENT_RAM;
			}
			return nullptr;
		}
		cluster = reinterpret_cast<Cluster*>(p);
		if (!cluster->loaded) {
			audioFileManager.loadingQueue.enqueueCluster(*cluster, priorityRating);
		}
		return cluster;
	}

	// CLUSTER_LOAD_IMMEDIATELY / _OR_ENQUEUE: must have it loaded now → acquire (full
	// materialize on a miss; this may block on I/O, which is the must-load-now contract).
	void* p = deluge_resource_acquire(mgr, asset, clusterIndex, sizeof(Cluster) + Cluster::size);
	if (p == nullptr) {
		if (error != nullptr) {
			*error = sample->unloadable ? Error::FILE_NOT_FOUND : Error::UNSPECIFIED;
		}
		return nullptr;
	}
	cluster = reinterpret_cast<Cluster*>(p);
	// Hit on a cluster that was prefetch-constructed but not yet read → read it now.
	if (!cluster->loaded) {
		bool ok = audioFileManager.readClusterData(*cluster, 0);
		audioFileManager.loadingQueue.erase(cluster); // it no longer needs the loader
		if (!ok) {
			if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE) {
				audioFileManager.loadingQueue.enqueueCluster(*cluster, priorityRating); // fall back to async
			}
			else {
				if (error != nullptr) {
					*error = Error::UNSPECIFIED;
				}
				return nullptr; // must-load-now failed; cluster stays resident+leased, caller may retry
			}
		}
	}
	return cluster;
}
