/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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
#include "storage/audio/audio_file_format.h"

#include <cstddef>
#include <cstdint>
#include <span>

class FileByteSource;
class Sample;

/// @brief Reads a wavetable's cycles one at a time, each as the cycle buffer the band builder takes: from a file
/// through
///        its byte source's cluster reads, or from a resident Sample's clusters (the in-memory conversion).
class WaveTableCycleReader {
public:
	struct Format {
		int32_t cycle_size; ///< samples per cycle, already clamped to the source length: the reader never clamps
		int32_t byte_depth;
		int32_t num_channels;
		RawDataFormat raw_data_format;
		uint32_t audio_data_start_bytes;
		uint32_t audio_data_length_bytes;
	};

	WaveTableCycleReader(FileByteSource& byte_source, Format format);
	/// @pre Every audio cluster of @p sample is resident and pinned.
	WaveTableCycleReader(Sample& sample, Format format);
	WaveTableCycleReader(const WaveTableCycleReader&) = delete;
	WaveTableCycleReader& operator=(const WaveTableCycleReader&) = delete;
	~WaveTableCycleReader(); ///< frees the in-memory path's block and frame buffers

	/// @brief Allocates the in-memory path's buffers, or positions the file at the audio data.
	[[nodiscard]] Error open();
	/// @brief The next cycle: format.cycle_size values, each the sample's Q31 value >> kCycleHeadroomBits.
	[[nodiscard]] Error read_cycle(std::span<int32_t> cycle);

private:
	/// @brief One sample's 32 bits as read, decoded to its Q31 value and stored at @p destination, which then advances.
	[[gnu::always_inline]] void store(uint32_t raw, int32_t*& destination) const;
	/// @brief Fills the in-memory path's block buffer with the file's bytes for cluster_index_.
	[[nodiscard]] Error load_sample_cluster();

	Format format_;
	FileByteSource* byte_source_ = nullptr;
	Sample* sample_ = nullptr;

	int32_t cluster_index_;
	/// Where the next sample starts in the current cluster; negative when it began in the previous one.
	int32_t byte_index_within_cluster_;
	uint32_t bytes_overlapping_from_last_cluster_ = 0;
	const char* source_buffer_ = nullptr;
	uint32_t bit_mask_;
	bool swapping_endianness_;
	bool need_to_misalign_data_;

	// In-memory path only: a reusable file-cluster-aligned block buffer holding the native audio bytes, refilled per
	// cluster from the streaming frame reader, with CACHE_LINE_SIZE of slack either side for the misaligned 32-bit
	// reads a few bytes before its start and just past its end; and the frame temp the reader fills with whole frames.
	void* sample_block_alloc_ = nullptr;
	void* sample_frame_alloc_ = nullptr;
	char* sample_block_buffer_ = nullptr;
	std::size_t sample_frame_temp_bytes_ = 0;
	uint32_t sample_source_id_ = 0;
	int32_t cluster_index_loaded_ = -1;
};
