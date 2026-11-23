/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// libdeluge/streaming_fill.h — the sample-streaming fill's per-asset geometry.
///
/// The Rust async fill task (deluge_platform_device's `streaming_loader.rs`, over `deluge_storage_sample_fill`'s
/// `native_begin`/`native_finish`) resolves each queued chunk's read geometry into a
/// `StreamingFillDescriptor`, performs the read, then converts/stitches/publishes. This header
/// carries the per-asset fill-context the task reads its geometry from. The loader queue's entry points
/// (wake, enqueue a chunk, drain, drained?) are in `streaming_loader.h`; the stream slot registry is in
/// `sample_stream.h`; the efatfs handle calls the fill reads through live in `file_io.h`.
#pragma once
#include "abi.h"
#include "libdeluge/types.h" // DelugeHandle, DelugeResource
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// @brief Descriptor the fill resolves from a queued chunk's geometry (`native_begin`): where to
///        DMA, and how much.
typedef struct StreamingFillDescriptor {
	uint8_t* dest;        ///< cluster payload base; write exactly num_sectors*512 bytes here
	uint32_t num_sectors; ///< sectors to read (accounts for a short final cluster) — the read LENGTH
	bool ok;              ///< false => skip the read (unloadable / geometry error); do not call finish
	DelugeHandle handle;  ///< efatfs read handle for this stream (streaming is efatfs-only); a @ref DelugeHandle
	uint32_t byte_offset; ///< absolute byte offset of this cluster within the file (efatfs read position)
} StreamingFillDescriptor;

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Per-asset geometry the native Rust fill task reads directly.
///
/// Registered by `deluge_streaming_set_fill_context` at sample-load, and read back through
/// `deluge_storage_sample_fill::fill_context_for(asset)` by `native_begin`/`native_finish`: the fill resolves
/// a queued chunk's descriptor from this table natively, with no C++ upcall. Carries the same
/// geometry the old `deluge::audio::stream::begin_fill` used to read off `Sample`/`Cluster` per
/// chunk, snapshotted once per asset instead.
typedef struct DelugeStreamingFillContext {
	DelugeHandle efatfs_handle;          ///< This sample's open embedded-fatfs read handle (0 = none yet).
	uint32_t audio_data_start_pos_bytes; ///< Offset from the start of the file to the first audio byte.
	uint64_t audio_data_length_bytes;    ///< Audio payload length in bytes; 0x8FFFFFFFFFFFFFFF = still recording.
	int32_t first_cluster_index_with_no_audio_data; ///< First cluster index past the end of the audio data.
	uint32_t cluster_size;                          ///< Cluster::size — bytes per (non-final) cluster.
	uint32_t cluster_size_magnitude;                ///< Cluster::size_magnitude — log2(cluster_size).
	uint8_t raw_data_format;                        ///< Sample::rawDataFormat (RawDataFormat's uint8_t representation).
	uint8_t byte_depth;   ///< Sample::byteDepth — bytes per channel-sample (e.g. 2 for 16-bit).
	uint8_t num_channels; ///< Sample::numChannels — channel count (1 = mono, 2 = stereo).
} DelugeStreamingFillContext;

/// @brief Register (or replace) asset @p asset's streaming fill-context. [task]
///
/// Called unconditionally from `SampleStream::ensure_resource_asset()` once the asset is defined
/// (and again from `open_read_stream()` if the efatfs handle becomes known afterwards) —
/// see `sample_stream.cpp`.
/// @param mgr   The resource manager instance (see deluge_streaming_resource_manager). Unused by the
///              Rust implementation today (there is exactly one process-wide manager); kept in the
///              signature for parity with the rest of the manager-scoped C ABI.
/// @param asset The asset id (`SampleStream::resource_asset_id()`).
/// @param ctx   The geometry to register for @p asset.
void deluge_streaming_set_fill_context(DelugeResource* mgr, uint32_t asset,
                                       DelugeStreamingFillContext ctx) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
