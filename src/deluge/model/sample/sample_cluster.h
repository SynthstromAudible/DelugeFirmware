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
#include <utility>

class Cluster;
class Sample;

// This is a quick list item within Sample storing minimal info about one Cluster (which often won't be loaded yet) of
// audio data for that Sample.
class SampleCluster {
public:
	SampleCluster() = default;
	~SampleCluster();

	// The destructor releases `cluster`, so ownership must transfer on move and copying is forbidden
	SampleCluster(const SampleCluster&) = delete;
	SampleCluster& operator=(const SampleCluster&) = delete;
	SampleCluster(SampleCluster&& other) noexcept
	    : sdAddress(other.sdAddress), cluster(std::exchange(other.cluster, nullptr)), minValue(other.minValue),
	      maxValue(other.maxValue), investigatedWholeLength(other.investigatedWholeLength) {}
	SampleCluster& operator=(SampleCluster&& other) noexcept {
		std::swap(sdAddress, other.sdAddress);
		std::swap(cluster, other.cluster);
		std::swap(minValue, other.minValue);
		std::swap(maxValue, other.maxValue);
		std::swap(investigatedWholeLength, other.investigatedWholeLength);
		return *this;
	}

	// TODO: This should return a std::expected<Cluster*, Error), removing the last parameter
	Cluster* getCluster(Sample* sample, uint32_t clusterIndex, int32_t loadInstruction = CLUSTER_ENQUEUE,
	                    uint32_t priorityRating = 0xFFFFFFFF, Error* error = nullptr);
	void ensureNoReason(Sample* sample);

	// In sectors. (Those 512 byte things. Not to be confused with clusters.)
	// 0 means invalid, and we check for this as a last resort before writing
	uint32_t sdAddress = 0;

	Cluster* cluster = nullptr; // May automatically be set to NULL if the Cluster needs to be deallocated (can only
	                            // happen if it has no "reasons" left)
	int8_t minValue = 127;
	int8_t maxValue = -128;
	bool investigatedWholeLength = false;
};
