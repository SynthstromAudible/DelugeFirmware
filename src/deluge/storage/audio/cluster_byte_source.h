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

#pragma once

#include "storage/audio/audio_byte_source.h"

class Sample;
class Cluster;

/// An AudioByteSource that streams a Sample's audio file off the SD card cluster-by-cluster, loading each
/// cluster on demand (CLUSTER_LOAD_IMMEDIATELY) as the cursor crosses into it. Used for the one-time header
/// parse during loading. The single held cluster's lease ("reason") is released in the destructor — RAII
/// replacing the manual cleanup the loader used to thread through a goto. Replaces SampleReader.
class ClusterByteSource final : public AudioByteSource {
public:
	/// `sample` must already have its cluster index table (clusters[].sdAddress) populated.
	ClusterByteSource(Sample& sample, uint32_t fileSize);
	~ClusterByteSource() override;

	ClusterByteSource(const ClusterByteSource&) = delete;
	ClusterByteSource& operator=(const ClusterByteSource&) = delete;

	[[nodiscard]] Error read(std::span<std::byte> dest) override;
	[[nodiscard]] uint32_t pos() override;
	void seekForwardTo(uint32_t absolutePos) override;
	[[nodiscard]] uint32_t size() const override { return fileSize_; }

private:
	/// Carry any overflow in `byteIndexWithinCluster_` into the cluster index, loading the new cluster.
	Error advanceClustersIfNecessary();

	Sample& sample_;
	uint32_t fileSize_;
	int32_t currentClusterIndex_ = -1;
	int32_t byteIndexWithinCluster_; // initialised to Cluster::size so the first read loads cluster 0
	Cluster* currentCluster_ = nullptr;
};
