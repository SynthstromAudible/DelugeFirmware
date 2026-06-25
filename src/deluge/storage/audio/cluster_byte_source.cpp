/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "storage/audio/cluster_byte_source.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"

ClusterByteSource::ClusterByteSource(Sample& sample, uint32_t fileSize)
    : sample_(sample), fileSize_(fileSize), byteIndexWithinCluster_(static_cast<int32_t>(Cluster::size)) {
}

ClusterByteSource::~ClusterByteSource() {
	if (currentCluster_ != nullptr) {
		audioFileManager.removeReasonFromCluster(*currentCluster_, "E030");
	}
}

uint32_t ClusterByteSource::pos() {
	return byteIndexWithinCluster_ + currentClusterIndex_ * Cluster::size;
}

void ClusterByteSource::seekForwardTo(uint32_t absolutePos) {
	byteIndexWithinCluster_ += static_cast<int32_t>(absolutePos - pos());
}

Error ClusterByteSource::advanceClustersIfNecessary() {
	const int32_t numClustersToAdvance = byteIndexWithinCluster_ >> Cluster::size_magnitude;
	if (numClustersToAdvance == 0) {
		return Error::NONE;
	}
	currentClusterIndex_ += numClustersToAdvance;
	byteIndexWithinCluster_ &= Cluster::size - 1;

	if (currentCluster_ != nullptr) {
		audioFileManager.removeReasonFromCluster(*currentCluster_, "E031");
	}
	currentCluster_ =
	    sample_.clusters[currentClusterIndex_].getCluster(&sample_, currentClusterIndex_, CLUSTER_LOAD_IMMEDIATELY);
	if (currentCluster_ == nullptr) {
		return Error::SD_CARD; // Failed to load cluster from card.
	}
	return Error::NONE;
}

Error ClusterByteSource::read(std::span<std::byte> dest) {
	if (static_cast<uint32_t>(pos() + dest.size()) > fileSize_) {
		return Error::FILE_CORRUPTED;
	}
	for (std::byte& out : dest) {
		if (const Error error = advanceClustersIfNecessary(); error != Error::NONE) {
			return error;
		}
		out = static_cast<std::byte>(currentCluster_->data[byteIndexWithinCluster_]);
		byteIndexWithinCluster_++;
	}
	return Error::NONE;
}
