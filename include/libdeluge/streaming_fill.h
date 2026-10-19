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

/// libdeluge/streaming_fill.h — the sample-streaming fill queue.
///
/// The Rust async fill task (deluge-bsp-rust's `streaming_loader.rs`, over `deluge_sample_fill`'s
/// `native_begin`/`native_finish`) resolves each queued chunk's read geometry into a
/// `StreamingFillDescriptor`, performs the read, then converts/stitches/publishes. This header
/// carries the queue's entry points (wake, enqueue a chunk, drain, drained?) and the per-asset
/// fill-context the task reads its geometry from. The streaming-asset lookups live in
/// `sample_stream.h`; the efatfs handle calls the fill reads through live in `file_io.h`.
#pragma once
#include "abi.h"
#include "libdeluge/types.h" // DelugeChunkBacking, DelugeHandle, DelugeResource
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

/// @brief Wake the async streaming-fill task out of its idle wait. [task] [audio]
///
/// The async task (`streaming_fill_task`, `streaming_loader.rs`) owns and drains the loader queue.
/// Called by the Rust residency code right after it enqueues a chunk on the loader queue: a reservation's
/// unfilled clusters (`deluge_sample_reader`'s `reservation.rs`) and a region acquire's miss
/// (`deluge_sample_source`'s `manager_residency.rs`). Harmless before the task first waits: the wake latches.
void deluge_streaming_signal_fill(void) DELUGE_NOEXCEPT;

/// @brief Enqueue a chunk for filling, and return immediately. [task]
///
/// A caller that cannot block -- because its predicate needs another task to be polled, and it is itself
/// inside the executor's poll -- requests the fill and returns to its own loop. The range reader's
/// fill route (deluge_sample_reader's `reader.rs`) is one such caller.
/// The chunk is enqueued under a queue-owned lease, so the fill survives the requester walking away.
/// @param chunk_backing The resident StreamedChunk to fill. NULL is ignored.
void deluge_streaming_request_chunk(DelugeChunkBacking* chunk_backing) DELUGE_NOEXCEPT;

/// @brief Wake the fill task to drain the loader queue, and return immediately. [task]
///
/// Pair with deluge_streaming_queue_drained().
void deluge_streaming_request_drain(void) DELUGE_NOEXCEPT;

/// @brief Report whether the loader queue holds nothing outstanding. Never waits. [task]
/// @return true when drained. Also true when there is no resource manager, since then there is
///         nothing to wait for and reporting otherwise would loop a poller forever.
bool deluge_streaming_queue_drained(void) DELUGE_NOEXCEPT;

/// @brief Per-asset geometry the native Rust fill task reads directly.
///
/// Registered by `deluge_streaming_set_fill_context` at sample-load, and read back through
/// `deluge_sample_fill::fill_context_for(asset)` by `native_begin`/`native_finish`: the fill resolves
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

/// @brief Buckets in a DelugeFillTraceHist: bucket 0 holds 0 µs, bucket i holds [2^(i-1), 2^i) µs, and the last
///        also holds everything longer (about 1 s and up).
#define DELUGE_FILL_TRACE_BUCKETS 22

/// @brief One phase of the fill trace: a histogram of its durations, in µs.
typedef struct DelugeFillTraceHist {
	uint32_t n;                                  ///< durations recorded
	uint32_t max_us;                             ///< the longest
	uint64_t total_us;                           ///< their sum, for the mean
	uint32_t buckets[DELUGE_FILL_TRACE_BUCKETS]; ///< log2 histogram, see DELUGE_FILL_TRACE_BUCKETS
} DelugeFillTraceHist;

/// @brief Where the streaming fill task's time goes, since the last deluge_fill_trace_reset().
///
/// The queue wait before a chunk is popped is DelugeResourceStats' queue_wait_*; this splits up what follows.
typedef struct DelugeFillTrace {
	DelugeFillTraceHist wake;    ///< from a wake signalled while the fill task waits, to its drain pass starting
	DelugeFillTraceHist fs_wait; ///< from asking for a chunk's read to taking the filesystem mutex
	DelugeFillTraceHist read;    ///< the read, with the mutex held
	DelugeFillTraceHist finish;  ///< the convert/stitch/publish tail after a successful read
	DelugeFillTraceHist gap;     ///< from one read's end to the next read's start in the same drain pass
	DelugeFillTraceHist pop;     ///< popping the next chunk off the loader queue
	DelugeFillTraceHist begin;   ///< resolving a popped chunk's read (its destination and file offset)
	DelugeFillTraceHist interaction_fs_wait; ///< an interaction-tier filesystem call waiting for the mutex
	DelugeFillTraceHist interaction_fs_hold; ///< an interaction-tier filesystem call holding the mutex
	/// Within one read: the time in single-sector card reads (the FAT and directory lookups of its seek) ...
	DelugeFillTraceHist read_meta;
	/// ... and in multi-sector card reads (its data).
	DelugeFillTraceHist read_data;
	uint32_t read_meta_sectors; ///< Single-sector card reads, over every read.
	uint32_t read_data_calls;   ///< Multi-sector card reads, over every read.
	uint32_t read_data_sectors; ///< The sectors those multi-sector reads moved.
	/// The longest any caller held the filesystem mutex, in µs, and where in the BSP's source it took it: the file,
	/// NUL-terminated (truncated to fit), and the line.
	uint32_t longest_fs_hold_us;
	uint32_t longest_fs_hold_line;
	char longest_fs_hold_file[48];
	/// Filesystem waits of 2 ms or more, by the tier (interaction, storage, audio) that last held the mutex.
	uint32_t long_fs_waits_by_tier[3];
	/// The longest of those waits, in µs, by the same tier.
	uint32_t long_fs_wait_max_by_tier[3];
} DelugeFillTrace;

/// @brief Copy the fill trace into @p out. [task]
/// @param out Where to write it. NULL is ignored.
void deluge_fill_trace_snapshot(DelugeFillTrace* out) DELUGE_NOEXCEPT;

/// @brief Zero the fill trace. [task]
void deluge_fill_trace_reset(void) DELUGE_NOEXCEPT;

/// @brief Time copying @p bytes out of uncached memory into cached memory: the copy every card read makes from the SD
/// driver's DMA buffer. [task]
/// @return The copy's duration in µs; 0 on host, or if the buffers could not be allocated.
uint32_t deluge_fill_trace_time_uncached_copy(uint32_t bytes) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
