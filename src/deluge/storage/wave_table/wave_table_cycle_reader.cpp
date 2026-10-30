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

#include "storage/wave_table/wave_table_cycle_reader.h"
#include "dsp-next/fixed/wavetable/band_builder.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "storage/audio/file_byte_source.h"
#include "storage/cluster/cluster.h"
#include "util/audio_format_helpers.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>

namespace {

[[nodiscard]] bool swaps_endianness(RawDataFormat format) {
	return format == RawDataFormat::ENDIANNESS_WRONG_32 || format == RawDataFormat::ENDIANNESS_WRONG_24
	       || format == RawDataFormat::ENDIANNESS_WRONG_16;
}

/// Native and unsigned 8-bit samples are read as the 32-bit word ending at the sample's last byte.
[[nodiscard]] bool misaligns_reads(RawDataFormat format) {
	return format == RawDataFormat::NATIVE || format == RawDataFormat::UNSIGNED_8;
}

} // namespace

WaveTableCycleReader::WaveTableCycleReader(FileByteSource& byte_source, Format format)
    : format_{format}, byte_source_{&byte_source},
      cluster_index_{static_cast<int32_t>(format.audio_data_start_bytes >> Cluster::size_magnitude)},
      byte_index_within_cluster_{static_cast<int32_t>(format.audio_data_start_bytes & (Cluster::size - 1))},
      bit_mask_{0xFFFFFFFF << ((4 - format.byte_depth) * 8)},
      swapping_endianness_{swaps_endianness(format.raw_data_format)},
      need_to_misalign_data_{misaligns_reads(format.raw_data_format)} {
}

WaveTableCycleReader::WaveTableCycleReader(Sample& sample, Format format)
    : format_{format}, sample_{&sample},
      cluster_index_{static_cast<int32_t>(format.audio_data_start_bytes >> Cluster::size_magnitude)},
      byte_index_within_cluster_{static_cast<int32_t>(format.audio_data_start_bytes & (Cluster::size - 1))},
      bit_mask_{0xFFFFFFFF << ((4 - format.byte_depth) * 8)},
      swapping_endianness_{swaps_endianness(format.raw_data_format)},
      need_to_misalign_data_{misaligns_reads(format.raw_data_format)} {
}

WaveTableCycleReader::~WaveTableCycleReader() {
	if (sample_block_alloc_ != nullptr) {
		deluge::memory::dealloc(sample_block_alloc_);
	}
	if (sample_frame_alloc_ != nullptr) {
		deluge::memory::dealloc(sample_frame_alloc_);
	}
}

Error WaveTableCycleReader::open() {
	if (sample_ != nullptr) {
		sample_block_alloc_ = deluge::memory::alloc_sdram(Cluster::size + CACHE_LINE_SIZE * 2);
		sample_frame_alloc_ = deluge::memory::alloc_sdram(Cluster::size + CACHE_LINE_SIZE * 2);
		if (sample_block_alloc_ == nullptr || sample_frame_alloc_ == nullptr) {
			return Error::INSUFFICIENT_RAM;
		}
		sample_block_buffer_ = static_cast<char*>(sample_block_alloc_) + CACHE_LINE_SIZE;
		sample_frame_temp_bytes_ = Cluster::size + CACHE_LINE_SIZE * 2;
		sample_source_id_ = deluge::sample::source_id_for(*sample_);
		return Error::NONE;
	}
	// In case the source isn't quite there yet, as with an AIFF file's offset.
	byte_source_->seekForwardTo(format_.audio_data_start_bytes);
	// The byte source owns the cluster buffer; the in-memory path points this at its block buffer per cluster.
	source_buffer_ = byte_source_->clusterBuffer();
	return Error::NONE;
}

void WaveTableCycleReader::store(uint32_t raw, int32_t*& destination) const {
	int32_t value32;
	if (format_.raw_data_format == RawDataFormat::FLOAT) {
		value32 = q31_from_float(std::bit_cast<float>(raw));
	}
	else {
		if (swapping_endianness_) {
			raw = swapEndianness32(raw);
		}
		raw &= bit_mask_;
		if (format_.raw_data_format == RawDataFormat::UNSIGNED_8) {
			raw += 0x80000000U;
		}
		value32 = static_cast<int32_t>(raw);
	}
	*(destination++) = value32 >> deluge::dsp::fixed::kCycleHeadroomBits;
}

Error WaveTableCycleReader::load_sample_cluster() {
	// Reproduce the file-cluster-aligned native bytes for cluster_index_: sample_block_buffer_[o] is the native byte at
	// file offset cluster_index_ * Cluster::size + o, for the audio bytes that fall in this cluster. The rest is
	// zero-filled so the misaligned edge reads never consume an uninitialised byte.
	const uint64_t cluster_file_base = static_cast<uint64_t>(cluster_index_) << Cluster::size_magnitude;
	const uint64_t audio_start = format_.audio_data_start_bytes;
	const uint64_t audio_end = audio_start + format_.audio_data_length_bytes;
	const uint64_t file_lo = std::max<uint64_t>(cluster_file_base, audio_start);
	const uint64_t file_hi = std::min<uint64_t>(cluster_file_base + Cluster::size, audio_end);

	memset(sample_block_buffer_, 0, Cluster::size);

	if (file_hi > file_lo) {
		const uint32_t stride = format_.byte_depth * format_.num_channels; // the reader serves whole frames
		const uint64_t audio_lo = file_lo - audio_start;
		const uint64_t audio_hi = file_hi - audio_start;
		const uint64_t first_frame = audio_lo / stride;
		const uint64_t last_frame_excl = (audio_hi + stride - 1) / stride;
		const auto num_frames = static_cast<uint32_t>(last_frame_excl - first_frame);

		// The first frame may begin a few bytes before file_lo when the cluster boundary falls mid-frame; the reader
		// lands whole frames stride-aligned in the temp, and the copy starts at that in-frame offset.
		const uint32_t frames_read = deluge_sample_read(sample_source_id_, first_frame, num_frames, sample_frame_alloc_,
		                                                sample_frame_temp_bytes_);
		if (frames_read < num_frames) {
			// A short read (card failure, end of file) left audio bytes unfilled: fail rather than zero-fill real
			// audio.
			return Error::SD_CARD;
		}

		const auto in_frame_offset = static_cast<uint32_t>(audio_lo % stride);
		const auto copy_bytes = static_cast<uint32_t>(audio_hi - audio_lo);
		const auto block_offset = static_cast<uint32_t>(file_lo - cluster_file_base);
		memcpy(sample_block_buffer_ + block_offset, static_cast<const char*>(sample_frame_alloc_) + in_frame_offset,
		       copy_bytes);
	}

	cluster_index_loaded_ = cluster_index_;
	source_buffer_ = sample_block_buffer_;
	return Error::NONE;
}

Error WaveTableCycleReader::read_cycle(std::span<int32_t> cycle) {
	assert(cycle.size() == static_cast<std::size_t>(format_.cycle_size));
	const int32_t byte_depth = format_.byte_depth;
	int32_t* destination = cycle.data();
	int32_t source_bytes_left = format_.cycle_size * byte_depth;

	// A cycle may span several clusters.
	do {
		if (sample_ != nullptr) {
			// A new cycle can start inside the cluster already loaded.
			if (cluster_index_ != cluster_index_loaded_) {
				if (const Error error = load_sample_cluster(); error != Error::NONE) {
					return error;
				}
			}
		}
		else {
			if (byte_index_within_cluster_ < 0) {
				// Puts the source at the start of the next cluster, so it reads it from the card.
				byte_source_->byteIndexWithinCluster() -= byte_index_within_cluster_;
			}
			if (const Error error = byte_source_->advanceClustersIfNecessary(); error != Error::NONE) {
				return error;
			}
		}

		// A sample that began at the end of the previous cluster: join its bytes from both.
		if (byte_index_within_cluster_ < 0) {
			int32_t misaligned_index = byte_index_within_cluster_;
			if (need_to_misalign_data_) {
				misaligned_index = misaligned_index - 4 + byte_depth;
			}
			const char* source = source_buffer_ + misaligned_index;

			// The previous cluster's bytes were read with the pointer already misaligned for the byte depth.
			bytes_overlapping_from_last_cluster_ &= (0xFFFFFFFFU >> ((4 + misaligned_index) * 8));
			const uint32_t bytes_overlapping_this_cluster =
			    *reinterpret_cast<const uint32_t*>(source) & (0xFFFFFFFFU << (-misaligned_index * 8));

			store(bytes_overlapping_this_cluster | bytes_overlapping_from_last_cluster_, destination);

			if (sample_ == nullptr) {
				// Plus byte_index_within_cluster_: the source already moved past it, above.
				byte_source_->byteIndexWithinCluster() += byte_depth + byte_index_within_cluster_;
			}

			source_bytes_left -= byte_depth;
			byte_index_within_cluster_ += byte_depth;
			if (source_bytes_left <= 0) {
				break;
			}
		}

		const char* source = &source_buffer_[byte_index_within_cluster_];
		// Stop before a final sample that runs past the end of the cluster.
		const char* source_stop_at = &source_buffer_[Cluster::size] - byte_depth + 1;
		if (source_stop_at - source > source_bytes_left) {
			source_stop_at = source + source_bytes_left;
		}

		const int32_t* const destination_started_at = destination;

		if (need_to_misalign_data_) {
			// Read each sample as the 32-bit word ending at its last byte.
			source = source - 4 + byte_depth;
			source_stop_at = source_stop_at - 4 + byte_depth;
		}

		// Everything before the end of the cycle or of the cluster, whichever is first.
		while (source < source_stop_at) {
			store(*reinterpret_cast<const uint32_t*>(source), destination);
			source += byte_depth;
		}

		const auto source_bytes_just_read = static_cast<int32_t>(destination - destination_started_at) * byte_depth;
		source_bytes_left -= source_bytes_just_read;
		byte_index_within_cluster_ += source_bytes_just_read;
		if (sample_ == nullptr) {
			byte_source_->byteIndexWithinCluster() += source_bytes_just_read;
		}

		// Less than one sample from the end of the cluster: the next starts here and ends in the next cluster.
		if (byte_index_within_cluster_ > Cluster::size - byte_depth) {
			bytes_overlapping_from_last_cluster_ = *reinterpret_cast<const uint32_t*>(source);
			byte_index_within_cluster_ -= Cluster::size;
			cluster_index_++;
		}
	} while (source_bytes_left > 0);

	return Error::NONE;
}
