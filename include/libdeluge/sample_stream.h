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

/// libdeluge/sample_stream.h — the streaming-file slot registry C-ABI.
///
/// Also the streaming-asset lookups: the resource manager a sample's chunks are leased from, a
/// stream's asset id, and where a streamed chunk's payload sits in its slot.
///
/// A fixed-capacity table of open efatfs streaming handles (`deluge_sample_stream` Rust crate),
/// each pairing an efatfs read handle with the resource-manager asset id + geometry a caller
/// supplies once known, registering the asset's `DelugeStreamingFillContext`
/// (`streaming_fill.h`) with the resource manager itself the moment both are present. Handles are
/// `DelugeHandle`s, numbered like every table handle (`types.h`: @ref DELUGE_HANDLE_NONE is none, and a
/// slot crosses as slot + 1), NOT a pointer — the registry owns a fixed slot table rather than
/// heap-boxing per-stream state.
///
/// This registry is the eventual home for `SampleStream`'s own held efatfs handle + geometry
/// (`sample_stream.cpp`'s `register_fill_context`); moving them here lets the C++ side stop
/// holding that state itself.
#ifndef LIBDELUGE_SAMPLE_STREAM_H
#define LIBDELUGE_SAMPLE_STREAM_H

#include "abi.h"
#include "types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief The resource-manager instance the streaming loader queue lives on. [task] [audio]
/// @return The single DelugeResource instance — the same one returned by
///         GeneralMemoryAllocator's resourceManager() — or null when it could not be created.
DelugeResource* deluge_streaming_resource_manager(void) DELUGE_NOEXCEPT;

/// @brief The resource-manager Asset id backing @p stream_backing, defining it first if needed. [task] [audio]
///
/// The region-port `open()` bridge: the reader passes its
/// `deluge::audio::stream::SampleStream` as `deluge_sample_source_open`'s `stream_backing`,
/// and the Rust cursor resolves it to `{deluge_streaming_resource_manager(), this accessor's
/// return}` before building its own residency provider — the same `{manager handle, asset id}`
/// pair `SampleStream::ensure_resource_asset()` itself defines against
/// `deluge_streaming_resource_manager()`. Calls the LAZY-init entry (`ensure_resource_asset()`,
/// not the plain `resource_asset_id()` getter): the asset may not be defined yet on a reader's
/// first open (e.g. a sample that has never been through `SampleStream::get_cluster()`), and this
/// is the one entry point that defines it on demand.
/// @param stream_backing The stream, as passed to deluge_sample_source_open. The definition
///                        (`sample_stream.cpp`) requires this to be a live `SampleStream`.
/// @return The Asset id, or `DELUGE_RESOURCE_NO_ASSET` when it could not be defined.
uint32_t deluge_sample_stream_asset_id(DelugeStreamBacking* stream_backing) DELUGE_NOEXCEPT;

/// @brief The byte offset a streamed chunk's payload sits at within its slab slot. The C++
///        slab setup sizes the shared cluster slot as max(this, ComputedChunk's kChunkPayloadOffset)
///        + Cluster::size + trailing guard. Rust-owned (deluge_sample_fill). [task]
uint32_t deluge_streamed_chunk_payload_offset(void) DELUGE_NOEXCEPT;

/// @brief The geometry a stream slot needs to register its asset's fill-context.
///
/// `streaming_fill.h`'s `DelugeStreamingFillContext` minus `efatfs_handle` (the registry supplies
/// that field itself, from the slot's own efatfs handle).
typedef struct DelugeSampleStreamGeometry {
	uint32_t audio_data_start_pos_bytes; ///< Offset from the start of the file to the first audio byte.
	uint64_t audio_data_length_bytes;    ///< Audio payload length in bytes; 0x8FFFFFFFFFFFFFFF = still recording.
	int32_t first_cluster_index_with_no_audio_data; ///< First cluster index past the end of the audio data.
	uint32_t cluster_size;                          ///< Cluster::size — bytes per (non-final) cluster.
	uint32_t cluster_size_magnitude;                ///< Cluster::size_magnitude — log2(cluster_size).
	uint8_t raw_data_format;                        ///< Sample::rawDataFormat (RawDataFormat's uint8_t representation).
	uint8_t byte_depth;   ///< Sample::byteDepth — bytes per channel-sample (e.g. 2 for 16-bit).
	uint8_t num_channels; ///< Sample::numChannels — channel count (1 = mono, 2 = stereo).
} DelugeSampleStreamGeometry;

/// @brief Open @p path for streaming reads and register a fresh slot for it. [task]
///
/// @param path           NUL-terminated absolute file path.
/// @param out_table_full Always written: true iff the failure was a full handle table, either
///                        this registry's or efatfs's own, so a caller can report exhaustion
///                        rather than "file not found". An efatfs handle opened for a slot that
///                        then finds the registry full is closed, never leaked.
/// @return The slot's handle on success; @ref DELUGE_HANDLE_NONE on failure.
DelugeHandle deluge_sample_stream_open(const char* path, bool* out_table_full) DELUGE_NOEXCEPT;

/// @brief Release @p handle's slot: close its efatfs handle (if any) and release its
///        resource-manager asset (if one was ever assigned). [task]
///
/// Idempotent — a no-op on @ref DELUGE_HANDLE_NONE or an already-closed handle.
/// @param handle A handle previously returned by deluge_sample_stream_open, or @ref DELUGE_HANDLE_NONE.
void deluge_sample_stream_close(DelugeHandle handle) DELUGE_NOEXCEPT;

/// @brief Store @p geo on @p handle's slot, registering its streaming fill-context with the
///        resource manager once both a geometry and a real asset id are present. [task]
///
/// No-op on an invalid/out-of-range/already-closed @p handle.
/// @param handle A handle previously returned by deluge_sample_stream_open.
/// @param geo    The geometry to store.
void deluge_sample_stream_set_geometry(DelugeHandle handle, DelugeSampleStreamGeometry geo) DELUGE_NOEXCEPT;

/// @brief @p handle's currently assigned resource-manager asset id. Nothing in the app or BSP calls it. [task]
///
/// Named `_get_` (not the plain `deluge_sample_stream_asset_id` its handle-based sibling setter's
/// naming would suggest): this header also declares the UNRELATED
/// `deluge_sample_stream_asset_id(DelugeStreamBacking*)` (the `SampleStream`-keyed accessor
/// `sample_stream.cpp` defines and `deluge_sample_source::abi` consumes) — reusing that exact name
/// here for a different signature would be a duplicate-symbol link error the moment both crates
/// link into the same binary.
/// @param handle A handle previously returned by deluge_sample_stream_open.
/// @return The asset id, or DELUGE_RESOURCE_NO_ASSET (0xFFFFFFFF) on an invalid handle or one
///         with no asset id assigned yet.
uint32_t deluge_sample_stream_get_asset_id(DelugeHandle handle) DELUGE_NOEXCEPT;

/// @brief Assign @p id as @p handle's resource-manager asset, registering its streaming
///        fill-context once both an asset id and a geometry are present. [task]
///
/// No-op on an invalid/out-of-range/already-closed @p handle.
/// @param handle A handle previously returned by deluge_sample_stream_open.
/// @param id     The resource-manager asset id to assign.
void deluge_sample_stream_set_asset_id(DelugeHandle handle, uint32_t id) DELUGE_NOEXCEPT;

/// @brief Read @p len bytes at absolute @p byte_offset of @p handle's open file into @p buf. [task]
///
/// Mirrors deluge_efatfs_read_at's success/byte-count split so a legitimate zero-byte read at/after
/// EOF stays distinct from a failure.
/// @param handle      A handle previously returned by deluge_sample_stream_open.
/// @param byte_offset Absolute byte offset within the file to read from.
/// @param buf         Destination buffer; up to @p len bytes are written.
/// @param len         Number of bytes to read.
/// @param out_read    On success, receives the bytes actually read (0 is a valid read at/after EOF).
/// @return `DELUGE_OK` with @p out_read written. On a failure @p out_read is left untouched:
///         `DELUGE_ERR_PARAM` for an invalid/out-of-range/already-closed @p handle, otherwise
///         @ref deluge_efatfs_read_at's own status.
DelugeStatus deluge_sample_stream_read_at(DelugeHandle handle, uint32_t byte_offset, uint8_t* buf, uint32_t len,
                                          uint32_t* out_read) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_SAMPLE_STREAM_H
