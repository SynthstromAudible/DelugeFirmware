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

#ifndef SAMPLEPITCHADJUSTMENT_H_
#define SAMPLEPITCHADJUSTMENT_H_

#include "r_typedefs.h"
#include "definitions.h"

class Sample;
class Cluster;

class SampleCache {
public:
	SampleCache(Sample* newSample, int newNumClusters, int newWaveformLengthBytes, int newPhaseIncrement,
	            int newTimeStretchRatio, int newSkipSamplesAtStart);
	~SampleCache();
	void clusterStolen(int clusterIndex);
	bool setupNewCluster(int cachedClusterIndex);
	Cluster* getCluster(int clusterIndex);
	void setWriteBytePos(int newWriteBytePos);

	int32_t writeBytePos;
#if ALPHA_OR_BETA_VERSION
	int numClusters;
#endif
	int waveformLengthBytes;
	Sample* sample;
	int32_t phaseIncrement;
	int32_t timeStretchRatio;
	int skipSamplesAtStart;

private:
	void unlinkClusters(int startAtIndex, bool beingDestructed);
	int getNumExistentClusters(int32_t thisWriteBytePos);
	void prioritizeNotStealingCluster(int clusterIndex);

	// This has to be last!!!
	Cluster* clusters[1]; // These are not initialized, and are only "valid" as far as writeBytePos dictates
};

#endif /* SAMPLEPITCHADJUSTMENT_H_ */
