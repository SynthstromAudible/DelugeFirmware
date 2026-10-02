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

#include "storage/audio/file_byte_source.h"
#include "memory/heaps.h"
#include "storage/cluster/cluster.h"
#include "storage/storage_manager.h"

// --- ReadSourceBlockReader (sample parse) ---

ReadSourceBlockReader::ReadSourceBlockReader(std::unique_ptr<deluge::audio::stream::ReadSource> readSource)
    : readSource_(std::move(readSource)) {
	// A private Cluster::size block buffer: the sample parse reads raw file bytes through the efatfs handle
	// and must NOT clobber the shared deserializer scratch buffer (a sample load can run mid-XML-parse, which
	// is using that buffer). Header parse only does bounded byte reads, so no edge slack is needed here.
	if (void* mem = deluge::memory::alloc_sdram(Cluster::size); mem != nullptr) {
		bufferBase_ = static_cast<std::byte*>(mem);
		buffer_ = std::span<std::byte>(bufferBase_, Cluster::size);
	}
}

ReadSourceBlockReader::~ReadSourceBlockReader() {
	if (bufferBase_ != nullptr) {
		deluge::memory::dealloc(bufferBase_);
	}
}

Error ReadSourceBlockReader::readBlock(uint32_t clusterIndex) {
	if (buffer_.empty()) {
		return Error::SD_CARD; // The block-buffer allocation failed at construction.
	}
	if (!readSource_->read(clusterIndex, buffer_)) {
		return Error::SD_CARD; // Failed to read the cluster from card.
	}
	return Error::NONE;
}

// --- DeserializerBlockReader (wavetable parse) ---

std::span<std::byte> DeserializerBlockReader::blockBuffer() {
	return std::span<std::byte>(reinterpret_cast<std::byte*>(smDeserializer.fileClusterBuffer), Cluster::size);
}

Error DeserializerBlockReader::readBlock([[maybe_unused]] uint32_t clusterIndex) {
	// Sequential read: the deserializer file cursor advances one block per call, so the cluster index is
	// implicit (unused).
	auto result = smDeserializer.file->read(
	    std::span<std::byte>(reinterpret_cast<std::byte*>(smDeserializer.fileClusterBuffer), Cluster::size));
	if (!result) {
		return Error::SD_CARD; // Failed to load cluster from card.
	}
	return Error::NONE;
}

// --- FileByteSource ---

FileByteSource::FileByteSource(std::unique_ptr<BlockReader> blockReader, uint32_t fileSize)
    : blockReader_(std::move(blockReader)), fileSize_(fileSize),
      byteIndexWithinCluster_(static_cast<int32_t>(Cluster::size)) {
}

uint32_t FileByteSource::pos() {
	return byteIndexWithinCluster_ + currentClusterIndex_ * Cluster::size;
}

void FileByteSource::seekForwardTo(uint32_t absolutePos) {
	byteIndexWithinCluster_ += static_cast<int32_t>(absolutePos - pos());
}

const char* FileByteSource::clusterBuffer() const {
	return reinterpret_cast<const char*>(blockReader_->blockBuffer().data());
}

Error FileByteSource::advanceClustersIfNecessary() {
	const int32_t numClustersToAdvance = byteIndexWithinCluster_ >> Cluster::size_magnitude;
	if (numClustersToAdvance == 0) {
		return Error::NONE;
	}
	currentClusterIndex_ += numClustersToAdvance;
	byteIndexWithinCluster_ &= Cluster::size - 1;
	return blockReader_->readBlock(currentClusterIndex_);
}

Error FileByteSource::read(std::span<std::byte> dest) {
	if (static_cast<uint32_t>(pos() + dest.size()) > fileSize_) {
		return Error::FILE_CORRUPTED;
	}
	for (std::byte& out : dest) {
		if (const Error error = advanceClustersIfNecessary(); error != Error::NONE) {
			return error;
		}
		out = blockReader_->blockBuffer()[byteIndexWithinCluster_];
		byteIndexWithinCluster_++;
	}
	return Error::NONE;
}
