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

#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>

class Cluster;
class Sample;

// This is a quick list item within Sample storing minimal info about one Cluster (which often won't be loaded yet) of
// audio data for that Sample.
class SampleCluster {
public:
	SampleCluster();
	~SampleCluster();
	Cluster* getCluster(Sample* sample, uint32_t clusterIndex, int32_t loadInstruction = CLUSTER_ENQUEUE,
	                    uint32_t priorityRating = 0xFFFFFFFF, uint8_t* error = NULL);
	void ensureNoReason(Sample* sample);

	uint32_t sdAddress; // In sectors. (Those 512 byte things. Not to be confused with clusters.)
	Cluster* cluster; // May automatically be set to NULL if the Cluster needs to be deallocated (can only happen if it
	                  // has no "reasons" left)
	int8_t minValue;
	int8_t maxValue;
	bool investigatedWholeLength;
};
