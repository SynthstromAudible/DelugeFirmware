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
#include "storage/audio/stream/read_source.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

/// @brief Raw block provider injected into `FileByteSource`.
///
/// Supplies a stable `Cluster::size` block buffer and fills it with a given cluster's raw file bytes — the
/// ONLY thing that differs between the sample and wavetable parse paths. See the two impls below.
class BlockReader {
public:
	virtual ~BlockReader() = default;

	/// @brief The `Cluster::size` block buffer this reader fills and that `FileByteSource::clusterBuffer()`
	///        exposes.
	///
	/// The base address is stable for the reader's lifetime (WaveTable::setup's misaligned band read captures
	/// it once and reads in place across cluster advances).
	/// @return The block buffer.
	[[nodiscard]] virtual std::span<std::byte> blockBuffer() = 0;

	/// @brief Fill blockBuffer() with cluster @p clusterIndex's raw file bytes.
	///
	/// On success the whole block is filled (fewer bytes only at EOF, which `FileByteSource`'s own
	/// `fileSize_` bound already prevents from being read past).
	/// @param clusterIndex Index of the cluster to fetch.
	/// @return Error::NONE on success, or Error::SD_CARD on a read failure.
	[[nodiscard]] virtual Error readBlock(uint32_t clusterIndex) = 0;
};

/// @brief Sample-parse provider: random-access raw efatfs read through the Sample's `SampleStream` read
///        source (`make_read_source()`).
///
/// Owns its own `Cluster::size` SDRAM block buffer. Takes NO resource-manager lease and touches NO
/// `StreamedChunk` — it reads raw file bytes off the already-open efatfs handle, so there is nothing to
/// release (the `ReadSource` unique_ptr is the only owned resource besides the buffer).
class ReadSourceBlockReader final : public BlockReader {
public:
	/// @brief Construct a reader that issues reads against @p readSource's already-open efatfs handle.
	/// @param readSource The Sample's read source (from `SampleStream::make_read_source()`); reads are issued
	///                   against the already-open efatfs handle it wraps.
	explicit ReadSourceBlockReader(std::unique_ptr<deluge::audio::stream::ReadSource> readSource);
	~ReadSourceBlockReader() override;

	ReadSourceBlockReader(const ReadSourceBlockReader&) = delete;
	ReadSourceBlockReader& operator=(const ReadSourceBlockReader&) = delete;

	/// @copydoc BlockReader::blockBuffer
	[[nodiscard]] std::span<std::byte> blockBuffer() override { return buffer_; }
	/// @copydoc BlockReader::readBlock
	[[nodiscard]] Error readBlock(uint32_t clusterIndex) override;

private:
	std::unique_ptr<deluge::audio::stream::ReadSource> readSource_;
	std::byte* bufferBase_ = nullptr; // owned SDRAM allocation (nullptr if the alloc failed)
	std::span<std::byte> buffer_{};   // the Cluster::size region within bufferBase_
};

/// @brief WaveTable-parse provider: reads the next `Cluster::size` block sequentially from the
///        StorageManager FatFS deserializer (`smDeserializer.file`) into `smDeserializer.fileClusterBuffer`.
///
/// It serves that same padded buffer through blockBuffer(), because `WaveTable::setup`'s zero-copy band
/// loop does misaligned 32-bit reads just before the buffer start and just past its end (see
/// storage_manager's `CACHE_LINE_SIZE` padding either side).
class DeserializerBlockReader final : public BlockReader {
public:
	/// @copydoc BlockReader::blockBuffer
	[[nodiscard]] std::span<std::byte> blockBuffer() override;
	/// @copydoc BlockReader::readBlock
	[[nodiscard]] Error readBlock(uint32_t clusterIndex) override;
};

/// @brief A block-buffered forward-reading `AudioByteSource` used for the one-time audio-file header parse.
///
/// It owns a within-cluster cursor over a `Cluster::size` block buffer whose bytes are fetched by an
/// injected `BlockReader`; the cursor logic (read / pos / seekForwardTo / advanceClustersIfNecessary) is
/// identical for both the sample and wavetable paths, which differ ONLY in where the block bytes come from.
///
/// The header parse drives it through the `AudioByteSource` surface; `WaveTable::setup`'s zero-copy band-build
/// loop drives it through the lower-level cluster accessors (`clusterBuffer` / `byteIndexWithinCluster` /
/// `advanceClustersIfNecessary`) — those exist because that loop reads misaligned 32-bit words straight out of
/// the block buffer and owns its own within-cluster cursor.
class FileByteSource final : public AudioByteSource {
public:
	/// @brief Construct a source that reads @p fileSize bytes through @p blockReader.
	/// @param blockReader Supplies the block buffer + fills it per cluster.
	/// @param fileSize    Total file size in bytes (the forward-read bound).
	FileByteSource(std::unique_ptr<BlockReader> blockReader, uint32_t fileSize);

	FileByteSource(const FileByteSource&) = delete;
	FileByteSource& operator=(const FileByteSource&) = delete;

	/// @copydoc AudioByteSource::read
	[[nodiscard]] Error read(std::span<std::byte> dest) override;
	/// @copydoc AudioByteSource::pos
	[[nodiscard]] uint32_t pos() override;
	/// @copydoc AudioByteSource::seekForwardTo
	void seekForwardTo(uint32_t absolutePos) override;
	/// @copydoc AudioByteSource::size
	[[nodiscard]] uint32_t size() const override { return fileSize_; }

	// --- Low-level cluster access for WaveTable::setup's zero-copy data read ---
	/// @brief Base address of the current cluster's `Cluster::size`-byte block buffer.
	/// @return The block buffer's base address.
	[[nodiscard]] const char* clusterBuffer() const;
	/// @brief The within-cluster cursor, owned by the source but mutated directly by setup's loop.
	///
	/// Setup's loop does misaligned 32-bit reads and carries an overlap across cluster boundaries.
	/// advanceClustersIfNecessary() reads the next cluster once this overflows past `Cluster::size`.
	/// @return A mutable reference to the cursor.
	[[nodiscard]] int32_t& byteIndexWithinCluster() { return byteIndexWithinCluster_; }
	/// @brief Carry any overflow in byteIndexWithinCluster() into the cluster index, fetching the next block.
	/// @return Error::NONE on success, or the underlying BlockReader::readBlock failure.
	[[nodiscard]] Error advanceClustersIfNecessary();

private:
	std::unique_ptr<BlockReader> blockReader_;
	uint32_t fileSize_;
	int32_t currentClusterIndex_ = -1;
	int32_t byteIndexWithinCluster_; // initialised to Cluster::size so the first read fetches cluster 0
};
