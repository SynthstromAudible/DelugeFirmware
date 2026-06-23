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
		int32_t numReasonsToBeLoaded = cluster->numReasonsToBeLoaded;
		if (cluster == audioFileManager.clusterBeingLoaded) {
			numReasonsToBeLoaded--;
		}

		if (numReasonsToBeLoaded) {
			D_PRINTLN("uh oh, some reasons left...  %d", numReasonsToBeLoaded);

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
		if (cluster->numReasonsToBeLoaded) {
			D_PRINTLN("Cluster has reason!  %d %d", cluster->numReasonsToBeLoaded, sample->filePath.c_str());

			if (cluster->numReasonsToBeLoaded >= 0) {
				FREEZE_WITH_ERROR("E068");
			}
			else {
				FREEZE_WITH_ERROR("E069");
			}
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

	// --- Resource-manager ownership decision (latches per Sample) ----------------
	// CLUSTER_DONT_LOAD means "allocate but don't read from the card" — recording, whose
	// clusters are written, not reconstructable — so it can't be a manager materialize and
	// such a Sample is latched legacy for life. Any read instruction makes the Sample
	// manager-owned if an Asset is available; if not, latch legacy so its clusters never
	// straddle both evictors (manager + CacheManager).
	if (loadInstruction == CLUSTER_DONT_LOAD) {
		sample->latchClustersLegacy();
	}
	uint32_t asset = (loadInstruction == CLUSTER_DONT_LOAD) ? DELUGE_RESOURCE_NO_ASSET : sample->ensureResourceAsset();
	if (loadInstruction != CLUSTER_DONT_LOAD && asset == DELUGE_RESOURCE_NO_ASSET) {
		sample->latchClustersLegacy();
	}

	if (asset != DELUGE_RESOURCE_NO_ASSET) {
		// Manager-owned read streaming. The manager owns residency + eviction for these clusters;
		// numReasonsToBeLoaded is a lease mirror (++ on a cache hit; a miss sets it to 1 in
		// construct/materialize). non-null `cluster` <=> manager-resident (on_evict nulls it).
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		bool wasResident = (cluster != nullptr);

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
			if (wasResident) {
				cluster->numReasonsToBeLoaded++;
			}
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
		if (wasResident) {
			cluster->numReasonsToBeLoaded++;
		}
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

	// --- Legacy path (recording / CLUSTER_DONT_LOAD / no manager available) ------

	// If the Cluster hasn't been created yet
	if (!cluster) {

		// If the file can no longer be found on the card, we're in trouble
		if (sample->unloadable) {
			D_PRINTLN("unloadable");
			if (error != nullptr) {
				*error = Error::FILE_NOT_FOUND;
			}
			return nullptr;
		}

		// D_PRINTLN("loading");
		cluster = Cluster::create(); // Adds 1 reason

		if (!cluster) {
			D_PRINTLN("couldn't allocate");
			if (error != nullptr) {
				*error = Error::INSUFFICIENT_RAM;
			}
			return nullptr;
		}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
		if (cluster->numReasonsToBeLoaded != 1) {
			FREEZE_WITH_ERROR("i005"); // Diversifying Qui's E341. It should actually be exactly 1
		}
		if (cluster->type != Cluster::Type::SAMPLE) {
			FREEZE_WITH_ERROR("E256"); // Cos I got E236
		}
#endif

		cluster->sample = sample;
		cluster->clusterIndex = clusterIndex;

		// Sometimes we don't actually want to load at all - if we're re-processing a WAV file and want to overwrite a
		// whole Cluster
		if (loadInstruction == CLUSTER_DONT_LOAD) {
			return cluster;
		}

		// If loading later...
		if (loadInstruction == CLUSTER_ENQUEUE) {
justEnqueue:

			if (ALPHA_OR_BETA_VERSION && cluster->type != Cluster::Type::SAMPLE) {
				FREEZE_WITH_ERROR("E236"); // Cos Chris F got an E205
			}

			// TODO: If that fails, it'll just get awkwardly forgotten about
			audioFileManager.loadingQueue.enqueueCluster(*cluster, priorityRating);

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				FREEZE_WITH_ERROR("i027"); // Diversifying Ron R's i004, which was diversifying Qui's E341
			}
#endif
		}

		// Or if want to try to load now...
		else { // CLUSTER_LOAD_IMMEDIATELY or CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE

			// cluster has (at least?) one reason - added above

			if (ALPHA_OR_BETA_VERSION && cluster->type != Cluster::Type::SAMPLE) {
				FREEZE_WITH_ERROR("E234"); // Cos Chris F got an E205
			}
			bool result = audioFileManager.loadCluster(*cluster, 1);

			// If that didn't work...
			if (!result) {

				// If also an acceptable option, then just enqueue it, and we'll keep the "reason" and return the
				// pointer
				if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE) {
					goto justEnqueue;
				}

				// Or if it was a must-load-now...
				// Free and remove our link to the unloaded Cluster - otherwise the next time we try to load it, it'd
				// still exist but never get enqueued for loading
				cluster->destroy(); // This removes the 1 reason that it'd still have

				if (error != nullptr) {
					// TODO: get actual error. Although sometimes it'd just be a "can't do it now
					// cos card's being accessed, and that's fine, thanks for checking."
					*error = Error::UNSPECIFIED;
				}
				cluster = nullptr;
			}
#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				FREEZE_WITH_ERROR("i026"); // Michael B got - insane.
			}
#endif
		}
	}

	// Or if it had previously been created...
	else {

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.1.3 have been getting E341.
		if (cluster && cluster->numReasonsToBeLoaded < 0) {
			FREEZE_WITH_ERROR("i028"); // bnhrsch got this!!
		}
#endif

		// If they'd prefer it loaded immediately and it's not loaded, try speeding loading along
		if ((loadInstruction == CLUSTER_LOAD_IMMEDIATELY || loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE)
		    && !cluster->loaded) {
			audioFileManager.loadAnyEnqueuedClusters();

			// If it's still not loaded and it was a must-load-now...
			if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY && !cluster->loaded) {
				D_PRINTLN("hurrying loading along failed for index:  %d", clusterIndex);
				if (error != nullptr) {
					*error = Error::UNSPECIFIED; // TODO: get actual error
				}
				return nullptr;
			}
		}

		cluster->addReason();

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.0.x have been getting E341.
		if (cluster && cluster->numReasonsToBeLoaded <= 0) {
			FREEZE_WITH_ERROR("i025"); // Diversifying Ron R's i004, which was diversifying Qui's E341
		}
#endif
	}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.0.x have been getting E341.
	if (cluster && cluster->numReasonsToBeLoaded <= 0) {
		FREEZE_WITH_ERROR("i004"); // Ron R got this! Diversifying Qui's E341
	}
#endif

	return cluster;
}
