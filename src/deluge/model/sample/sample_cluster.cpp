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
#include "hid/display/display.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "util/functions.h"

SampleCluster::SampleCluster() {
	cluster = NULL;

	investigatedWholeLength = false;
	minValue = 127;
	maxValue = -128;
	sdAddress = 0; // 0 means invalid, and we check for this as a last resort before writing
}

SampleCluster::~SampleCluster() {
	if (cluster) {

#if ALPHA_OR_BETA_VERSION
		int32_t numReasonsToBeLoaded = cluster->numReasonsToBeLoaded;
		if (cluster == audioFileManager.clusterBeingLoaded) {
			numReasonsToBeLoaded--;
		}

		if (numReasonsToBeLoaded) {
			Debug::print("uh oh, some reasons left... ");
			Debug::println(numReasonsToBeLoaded);

			display->freezeWithError(
			    "E036"); // Bay_Mud got this, and thinks a FlashAir card might have been a catalyst. It still "shouldn't" be able to happen though.
		}
#endif
		audioFileManager.deallocateCluster(cluster);
	}
}

void SampleCluster::ensureNoReason(Sample* sample) {
	if (cluster) {
		if (cluster->numReasonsToBeLoaded) {
			Debug::print("Cluster has reason! ");
			Debug::println(cluster->numReasonsToBeLoaded);
			Debug::println(sample->filePath.get());

			if (cluster->numReasonsToBeLoaded >= 0) {
				display->freezeWithError("E068");
			}
			else {
				display->freezeWithError("E069");
			}
			delayMS(50);
		}
	}
}

// Calling this will add a reason to the loaded Cluster!
// priorityRating is only relevant if enqueuing.
Cluster* SampleCluster::getCluster(Sample* sample, uint32_t clusterIndex, int32_t loadInstruction,
                                   uint32_t priorityRating, uint8_t* error) {

	if (error) {
		*error = NO_ERROR;
	}

	// If the Cluster hasn't been created yet
	if (!cluster) {

		// If the file can no longer be found on the card, we're in trouble
		if (sample->unloadable) {
			Debug::println("unloadable");
			if (error) {
				*error = ERROR_FILE_NOT_FOUND;
			}
			return NULL;
		}

		//Debug::println("loading");
		cluster = audioFileManager.allocateCluster(); // Adds 1 reason

		if (!cluster) {
			Debug::println("couldn't allocate");
			if (error) {
				*error = ERROR_INSUFFICIENT_RAM;
			}
			return NULL;
		}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
		if (cluster->numReasonsToBeLoaded != 1) {
			display->freezeWithError("i005"); // Diversifying Qui's E341. It should actually be exactly 1
		}
		if (cluster->type != ClusterType::Sample) {
			display->freezeWithError("E256"); // Cos I got E236
		}
#endif

		cluster->sample = sample;
		cluster->clusterIndex = clusterIndex;

		// Sometimes we don't actually want to load at all - if we're re-processing a WAV file and want to overwrite a whole Cluster
		if (loadInstruction == CLUSTER_DONT_LOAD) {
			return cluster;
		}

		// If loading later...
		if (loadInstruction == CLUSTER_ENQUEUE) {
justEnqueue:

			if (ALPHA_OR_BETA_VERSION && cluster->type != ClusterType::Sample) {
				display->freezeWithError("E236"); // Cos Chris F got an E205
			}

			audioFileManager.enqueueCluster(
			    cluster, priorityRating); // TODO: If that fails, it'll just get awkwardly forgotten about
#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				display->freezeWithError("i027"); // Diversifying Ron R's i004, which was diversifying Qui's E341
			}
#endif
		}

		// Or if want to try to load now...
		else { // CLUSTER_LOAD_IMMEDIATELY or CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE

			// cluster has (at least?) one reason - added above

			if (ALPHA_OR_BETA_VERSION && cluster->type != ClusterType::Sample) {
				display->freezeWithError("E234"); // Cos Chris F got an E205
			}
			bool result = audioFileManager.loadCluster(cluster, 1);

			// If that didn't work...
			if (!result) {

				// If also an acceptable option, then just enqueue it, and we'll keep the "reason" and return the pointer
				if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE) {
					goto justEnqueue;
				}

				// Or if it was a must-load-now...
				// Free and remove our link to the unloaded Cluster - otherwise the next time we try to load it, it'd still exist but never get enqueued for loading
				audioFileManager.deallocateCluster(cluster); // This removes the 1 reason that it'd still have

				if (error) {
					*error =
					    ERROR_UNSPECIFIED; // TODO: get actual error. Although sometimes it'd just be a "can't do it now cos card's being accessed, and that's fine, thanks for checking."
				}
				cluster = NULL;
			}
#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on on V4.0.x have been getting E341.
			if (cluster && cluster->numReasonsToBeLoaded <= 0) {
				display->freezeWithError("i026"); // Michael B got - insane.
			}
#endif
		}
	}

	// Or if it had previously been created...
	else {

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.1.3 have been getting E341.
		if (cluster && cluster->numReasonsToBeLoaded < 0) {
			display->freezeWithError("i028"); // bnhrsch got this!!
		}
#endif

		// If they'd prefer it loaded immediately and it's not loaded, try speeding loading along
		if ((loadInstruction == CLUSTER_LOAD_IMMEDIATELY || loadInstruction == CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE)
		    && !cluster->loaded) {
			audioFileManager.loadAnyEnqueuedClusters();

			// If it's still not loaded and it was a must-load-now...
			if (loadInstruction == CLUSTER_LOAD_IMMEDIATELY && !cluster->loaded) {
				Debug::print("hurrying loading along failed for index: ");
				Debug::println(clusterIndex);
				if (error) {
					*error = ERROR_UNSPECIFIED; // TODO: get actual error
				}
				return NULL;
			}
		}

		audioFileManager.addReasonToCluster(cluster);

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.0.x have been getting E341.
		if (cluster && cluster->numReasonsToBeLoaded <= 0) {
			display->freezeWithError("i025"); // Diversifying Ron R's i004, which was diversifying Qui's E341
		}
#endif
	}

#if 1 || ALPHA_OR_BETA_VERSION // Switching permanently on for now, as users on V4.0.x have been getting E341.
	if (cluster && cluster->numReasonsToBeLoaded <= 0) {
		display->freezeWithError("i004"); // Ron R got this! Diversifying Qui's E341
	}
#endif

	return cluster;
}
