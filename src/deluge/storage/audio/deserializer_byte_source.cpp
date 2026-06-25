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

#include "storage/audio/deserializer_byte_source.h"
#include "definitions_cxx.hpp"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"

DeserializerByteSource::DeserializerByteSource(uint32_t fileSize)
    : fileSize_(fileSize), byteIndexWithinCluster_(static_cast<int32_t>(Cluster::size)) {
}

uint32_t DeserializerByteSource::pos() {
	return byteIndexWithinCluster_ + currentClusterIndex_ * Cluster::size;
}

void DeserializerByteSource::seekForwardTo(uint32_t absolutePos) {
	byteIndexWithinCluster_ += static_cast<int32_t>(absolutePos - pos());
}

const char* DeserializerByteSource::clusterBuffer() const {
	return smDeserializer.fileClusterBuffer;
}

Error DeserializerByteSource::readNewCluster() {
	UINT bytesRead;
	FRESULT result = f_read(&smDeserializer.readFIL, smDeserializer.fileClusterBuffer, Cluster::size, &bytesRead);
	if (result != FR_OK) {
		return Error::SD_CARD; // Failed to load cluster from card.
	}
	return Error::NONE;
}

Error DeserializerByteSource::advanceClustersIfNecessary() {
	const int32_t numClustersToAdvance = byteIndexWithinCluster_ >> Cluster::size_magnitude;
	if (numClustersToAdvance == 0) {
		return Error::NONE;
	}
	currentClusterIndex_ += numClustersToAdvance;
	byteIndexWithinCluster_ &= Cluster::size - 1;
	return readNewCluster();
}

Error DeserializerByteSource::read(std::span<std::byte> dest) {
	if (static_cast<uint32_t>(pos() + dest.size()) > fileSize_) {
		return Error::FILE_CORRUPTED;
	}
	for (std::byte& out : dest) {
		if (const Error error = advanceClustersIfNecessary(); error != Error::NONE) {
			return error;
		}
		out = static_cast<std::byte>(smDeserializer.fileClusterBuffer[byteIndexWithinCluster_]);
		byteIndexWithinCluster_++;
	}
	return Error::NONE;
}
