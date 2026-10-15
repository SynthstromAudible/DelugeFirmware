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
#include "extern.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include <cstddef>

SampleCluster::~SampleCluster() {
	if (cluster) {

		int32_t numReasonsToBeLoaded = cluster->numReasonsToBeLoaded;
		if (cluster == audioFileManager.clusterBeingLoaded) {
			numReasonsToBeLoaded--;
		}
#if ALPHA_OR_BETA_VERSION
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
			D_PRINTLN("Cluster has reason!  %d %d", cluster->numReasonsToBeLoaded, sample->filePath.get());

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

		if (cluster->numReasonsToBeLoaded != 1) {
			FREEZE_WITH_ERROR("i005"); // Diversifying Qui's E341. It should actually be exactly 1
		}
		if (cluster->type != Cluster::Type::SAMPLE) {
			FREEZE_WITH_ERROR("E256"); // Cos I got E236
		}

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
			CriticalSectionGuard guard;
			audioFileManager.loadingQueue.enqueueCluster(*cluster, priorityRating, guard);

			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				FREEZE_WITH_ERROR("i027"); // Diversifying Ron R's i004, which was diversifying Qui's E341
			}
		}

		// Inside routineForSD() or similar, waiting for the card busy-waits rather than letting the audio routine run,
		// so if enqueuing is fine, don't load it here
		else if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE && sdRoutineLock) {
			goto justEnqueue;
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
			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				FREEZE_WITH_ERROR("i026"); // Michael B got - insane.
			}
		}
	}

	// Or if it had previously been created...
	else {

		if (GeneralMemoryAllocator::get().getRegion(cluster) != MEMORY_REGION_STEALABLE) {
			FREEZE_WITH_ERROR("invalid");
		}
		if (cluster && cluster->numReasonsToBeLoaded < 0) {
			FREEZE_WITH_ERROR("i028"); // bnhrsch got this!!
		}

		// If it's still waiting to load, it may have been enqueued at a lower priority than we need now - e.g. if it
		// was prefetched, or enqueued for drawing a waveform
		if (!cluster->loaded) {
			CriticalSectionGuard guard;
			audioFileManager.loadingQueue.raisePriority(cluster, priorityRating, guard);
		}

		// Our reason has to be held before loading it below, so it can't be let go of meanwhile
		cluster->addReason();

		// If they'd prefer it loaded immediately and it's not loaded, load just it - not everything ahead of it in the
		// queue. Unless enqueuing is fine and we're inside routineForSD() or similar, where waiting for the card
		// busy-waits rather than letting the audio routine run.
		bool wantItNow = loadInstruction == CLUSTER_LOAD_IMMEDIATELY
		                 || (loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE && !sdRoutineLock);
		if (wantItNow && !cluster->loaded) {
			bool loaded = audioFileManager.loadEnqueuedClusterNow(*cluster);

			// If it was a must-load-now...
			if (!loaded && loadInstruction == CLUSTER_LOAD_IMMEDIATELY) {
				D_PRINTLN("hurrying loading along failed for index:  %d", clusterIndex);
				if (error != nullptr) {
					*error = Error::UNSPECIFIED; // TODO: get actual error
				}
				audioFileManager.removeReasonFromCluster(*cluster, "4462"); // May delete it
				return nullptr;
			}
		}

		if (cluster && cluster->numReasonsToBeLoaded <= 0) {
			FREEZE_WITH_ERROR("i025"); // Diversifying Ron R's i004, which was diversifying Qui's E341
		}
	}

	if (cluster && cluster->numReasonsToBeLoaded <= 0) {
		FREEZE_WITH_ERROR("i004"); // Ron R got this! Diversifying Qui's E341
	}

	return cluster;
}
