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

/// An AudioByteSource over the StorageManager FatFS deserializer (the global `smDeserializer`): it pulls the
/// file in `Cluster::size` blocks via `f_read` into `smDeserializer.fileClusterBuffer`. Used for the WaveTable
/// load path. The header parse drives it through the byte-stream surface; `WaveTable::setup`'s zero-copy
/// band-build loop drives it through the lower-level cluster accessors (`clusterBuffer` /
/// `byteIndexWithinCluster` / `advanceClustersIfNecessary`) — those exist because that loop reads misaligned
/// 32-bit words straight out of the cluster buffer and owns its own within-cluster cursor. Replaces
/// WaveTableReader + the AudioFileReader base. The file must already be open (StorageManager::openFilePointer
/// onto `smDeserializer`) before constructing.
class DeserializerByteSource final : public AudioByteSource {
public:
	explicit DeserializerByteSource(uint32_t fileSize);

	[[nodiscard]] Error read(std::span<std::byte> dest) override;
	[[nodiscard]] uint32_t pos() override;
	void seekForwardTo(uint32_t absolutePos) override;
	[[nodiscard]] uint32_t size() const override { return fileSize_; }

	// --- Low-level cluster access for WaveTable::setup's zero-copy data read ---
	/// Base address of the current cluster's `Cluster::size`-byte buffer (the deserializer's `fileClusterBuffer`).
	[[nodiscard]] const char* clusterBuffer() const;
	/// The within-cluster cursor, owned by the source but mutated directly by setup's loop (it does misaligned
	/// 32-bit reads and carries an overlap across cluster boundaries). `advanceClustersIfNecessary` reads the
	/// next cluster once this overflows past `Cluster::size`.
	[[nodiscard]] int32_t& byteIndexWithinCluster() { return byteIndexWithinCluster_; }
	/// Carry any overflow in `byteIndexWithinCluster()` into the cluster index, `f_read`-ing the next cluster.
	[[nodiscard]] Error advanceClustersIfNecessary();

private:
	Error readNewCluster();

	uint32_t fileSize_;
	int32_t currentClusterIndex_ = -1;
	int32_t byteIndexWithinCluster_; // initialised to Cluster::size so the first read loads cluster 0
};
