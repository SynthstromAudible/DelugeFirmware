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

#pragma once

#include "definitions_cxx.hpp"

class Sample;
class Cluster;

class SampleCache {
public:
	SampleCache(Sample* newSample, int32_t newNumClusters, int32_t newWaveformLengthBytes, int32_t newPhaseIncrement,
	            int32_t newTimeStretchRatio, int32_t newSkipSamplesAtStart, bool newReversed);
	~SampleCache();
	void clusterStolen(int32_t clusterIndex);
	// Resource-manager on_evict for this cache's clusters: ~Cluster the evicted slot (the
	// manager frees its backing) then run clusterStolen's truncation. Only the highest-index
	// cluster is ever evicted (the asset is evict_tail_first), so nothing cascades.
	void onCacheEvict(int32_t clusterIndex);
	bool setupNewCluster(int32_t cachedClusterIndex);
	Cluster* getCluster(int32_t clusterIndex);
	void setWriteBytePos(int32_t newWriteBytePos);

	// Resource-manager Asset id for this cache's SAMPLE_CACHE clusters. The manager is the sole
	// SDRAM evictor, so this is always valid after construction (asset-table exhaustion is fatal).
	// 0xFFFFFFFF == DELUGE_RESOURCE_NO_ASSET (the unset sentinel before construct / after retire).
	uint32_t resourceAssetId{0xFFFFFFFFu};

	int32_t writeBytePos;
#if ALPHA_OR_BETA_VERSION
	int32_t numClusters;
#endif
	int32_t waveformLengthBytes;
	Sample* sample;
	int32_t phaseIncrement;
	int32_t timeStretchRatio;
	int32_t skipSamplesAtStart;
	bool reversed;

private:
	void unlinkClusters(int32_t startAtIndex, bool beingDestructed);
	int32_t getNumExistentClusters(int32_t thisWriteBytePos);

	// This has to be last!!!
	Cluster* clusters[1]{}; // These are not initialized, and are only "valid" as far as writeBytePos dictates
};
