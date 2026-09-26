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

/// libdeluge/streaming_fill.h — the cluster-fill descriptor + resource-manager/asset C-ABI.
///
/// The Rust async fill task (deluge-bsp-rust's `streaming_loader.rs`, over `deluge_sample_fill`'s
/// `native_begin`/`native_finish`) resolves each queued chunk's read geometry into a
/// `StreamingFillDescriptor`, performs the read, then converts/stitches/publishes. This header
/// carries the shared FFI types (the descriptor) plus the resource-manager/asset and
/// signal/readiness entry points that cross the C++/Rust boundary. The async task calls
/// `native_begin`/`native_finish` directly.
#pragma once
#include "libdeluge/types.h" // DelugeStatus
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct DelugeResource DelugeResource;

/// @brief Descriptor the fill resolves from a queued chunk's geometry (`native_begin`): where to
///        DMA, and how much.
typedef struct StreamingFillDescriptor {
	uint8_t* dest;        ///< cluster payload base; write exactly num_sectors*512 bytes here
	uint32_t num_sectors; ///< sectors to read (accounts for a short final cluster) — the read LENGTH
	bool ok;              ///< false => skip the read (unloadable / geometry error); do not call finish
	uint32_t handle;      ///< efatfs file handle for this stream (streaming read is efatfs-only)
	uint32_t byte_offset; ///< absolute byte offset of this cluster within the file (efatfs read position)
} StreamingFillDescriptor;

#ifdef __cplusplus
extern "C" {
#endif

/// @brief The resource-manager instance the streaming loader queue lives on.
/// @return The single DelugeResource instance — the same one returned by
///         GeneralMemoryAllocator's resourceManager().
DelugeResource* deluge_streaming_resource_manager(void);

/// @brief The resource-manager Asset id backing @p stream_backing, defining it first if needed.
///
/// The region-port `open()` bridge: the reader passes its
/// `deluge::audio::stream::SampleStream*` as `deluge_sample_source_open`'s opaque `stream_backing`,
/// and the Rust cursor resolves it to `{deluge_streaming_resource_manager(), this accessor's
/// return}` before building its own residency provider — the same `{manager handle, asset id}`
/// pair `SampleStream::ensure_resource_asset()` itself defines against
/// `deluge_streaming_resource_manager()`. Calls the LAZY-init entry (`ensure_resource_asset()`,
/// not the plain `resource_asset_id()` getter): the asset may not be defined yet on a reader's
/// first open (e.g. a sample that has never been through `SampleStream::get_cluster()`), and this
/// is the one entry point that defines it on demand.
/// @param stream_backing Opaque `SampleStream*` backing pointer, as passed to
///                        deluge_sample_source_open. The real definition (`sample_stream.cpp`)
///                        requires this to be a live `SampleStream*`; the `__attribute__((weak))`
///                        no-op fallback (`async_fill.cpp`, for build configs without a real
///                        `SampleStream`) ignores it.
/// @return The Asset id, or DELUGE_RESOURCE_NO_ASSET on the weak fallback.
uint32_t deluge_sample_stream_asset_id(void* stream_backing);

/// @brief The byte offset a streamed chunk's payload sits at within its slab slot. The C++
///        slab setup sizes the shared cluster slot as max(this, ComputedChunk's kChunkPayloadOffset)
///        + Cluster::size + trailing guard. Rust-owned (deluge_sample_fill).
uint32_t deluge_streamed_chunk_payload_offset(void);

/// @brief Whether the Rust async streaming-fill task (`streaming_fill_task`, cargo feature
///        `async_streaming_loader`) owns the loader queue on this build/BSP.
///
/// A runtime getter rather than a compile-time `#define`: the C++ `deluge_app` is built once by
/// CMake and linked into whichever BSP, so a Rust cargo feature can't reach a C++ preprocessor
/// define. True only on the Rust/Embassy BSP with `async_streaming_loader` enabled (the real
/// implementation lives in `streaming_loader.rs`); every other BSP/config links the
/// `__attribute__((weak))` fallback in `async_fill.cpp`, which always returns false. When true, the
/// async task owns and drains the (streaming-only) loader queue itself; there is no C++-side pump
/// anymore (the old `loader::pump()`/`request_pump()` were deleted with `loader.cpp`).
/// @return true if the async task owns the loader queue on this build/BSP.
bool deluge_streaming_async_active(void);

/// @brief Wake the async streaming-fill task out of its idle wait.
///
/// Called unconditionally at every streaming CLUSTER_ENQUEUE site (`sample_stream.cpp`), after
/// `deluge_resource_loader_enqueue()`. Harmless when the async backing isn't active: on the
/// Embassy BSP with the feature off it signals a `Signal` nobody awaits; on every other BSP it
/// hits the weak no-op fallback in `async_fill.cpp`.
void deluge_streaming_signal_fill(void);

/// @brief Enqueues a chunk for filling and returns immediately
///
/// A caller that cannot block -- because its predicate needs another task to be polled, and it is itself
/// inside the executor's poll -- requests the fill, returns to its own loop, and asks
/// deluge_streaming_chunk_ready() next tick. The range reader's async-BSP fill route
/// (deluge_sample_reader's `reader.rs`) is one such caller.
/// The chunk is enqueued under a queue-owned lease, so the fill survives the requester walking away.
/// @param chunk_backing the resident StreamedChunk to fill. Null is ignored.
void deluge_streaming_request_chunk(void* chunk_backing);

/// @brief Reports whether a requested chunk has landed. Never waits.
/// @param chunk_backing the chunk previously passed to deluge_streaming_request_chunk().
/// @return true once the fill has completed.
bool deluge_streaming_chunk_ready(void* chunk_backing);

/// @brief Wakes the fill task to drain the loader queue and returns immediately
///
/// Pair with deluge_streaming_queue_drained().
void deluge_streaming_request_drain(void);

/// @brief Reports whether the loader queue holds nothing outstanding. Never waits.
/// @return true when drained. Also true when there is no resource manager, since then there is
///         nothing to wait for and reporting otherwise would loop a poller forever.
bool deluge_streaming_queue_drained(void);

/// @brief Open a sample file for streaming reads through embedded-fatfs, writing its handle out.
///
/// The sync→async bridge for the streaming READ path: C++ calls this synchronously at sample-load,
/// and the Rust implementation (`efatfs_fs.rs`, cargo feature `efatfs_streaming`) bridges to the
/// async handle table by blocking on the async open.
/// @param path           NUL-terminated absolute file path.
/// @param out_handle     Receives the opaque handle on success; untouched on failure.
/// @param out_table_full Always written (on both success and failure): true iff the open failed
///                       specifically because the streaming-read handle table has no free slot (as
///                       opposed to the file genuinely not existing, or the FS being unmounted) --
///                       the caller maps a `true` value to a dedicated `Error::TOO_MANY_OPEN_STREAMS`
///                       rather than the misleading `Error::FILE_NOT_FOUND`. Always `false` when
///                       @p out_handle was written.
/// @return true if the file was opened and @p out_handle written; false (caller falls back to the
///         C-FatFS sector path) if the path/pointer is invalid, the FS is
///         unmounted, or the open failed. Every non-efatfs BSP/config links the weak no-op fallback
///         in `async_fill.cpp`, which always returns false and writes `*out_table_full = false`.
bool deluge_efatfs_open(const char* path, uint32_t* out_handle, bool* out_table_full);

/// @brief Close a streaming file handle previously returned by deluge_efatfs_open.
///
/// @param handle The handle to close. A no-op on every non-efatfs BSP/config (weak fallback).
void deluge_efatfs_close(uint32_t handle);

/// @brief Synchronously read @p count bytes at absolute @p byte_offset of the file behind @p handle.
///
/// The sync-path counterpart of the async `ProdOps::read` efatfs branch: the range reader's
/// synchronous `fill_now` (`deluge_sample_reader`) reads through this. The Rust implementation
/// (`efatfs_fs.rs` / `efatfs_host_shim.rs`, cargo feature `efatfs_streaming`) bridges to the async
/// handle table by blocking on the async read.
/// @param handle     A handle previously returned by deluge_efatfs_open.
/// @param byte_offset Absolute byte offset within the file to read from.
/// @param dst        Destination buffer; exactly @p count bytes are written on success.
/// @param count      Number of bytes to read.
/// @param out_read   Receives the bytes read on success (== @p count); untouched on failure.
/// @return true iff the full @p count bytes were read. false (unmounted, bad handle, or a
///         short/failed read) leaves @p dst partially written and @p out_read untouched — the caller
///         must treat it as a read error. Every non-efatfs BSP/config links the weak no-op fallback.
bool deluge_efatfs_read_at(uint32_t handle, uint32_t byte_offset, void* dst, uint32_t count, uint32_t* out_read);

/// @brief Per-asset geometry the native Rust fill task reads directly.
///
/// Registered by `deluge_streaming_set_fill_context` at sample-load, and read back through
/// `deluge_sample_fill::fill_context_for(asset)` by `native_begin`/`native_finish`: the fill resolves
/// a queued chunk's descriptor from this table natively, with no C++ upcall. Carries the same
/// geometry the old `deluge::audio::stream::begin_fill` used to read off `Sample`/`Cluster` per
/// chunk, snapshotted once per asset instead.
typedef struct DelugeStreamingFillContext {
	uint32_t efatfs_handle;              ///< This sample's open embedded-fatfs read handle (0 = none yet).
	uint32_t audio_data_start_pos_bytes; ///< Offset from the start of the file to the first audio byte.
	uint64_t audio_data_length_bytes;    ///< Audio payload length in bytes; 0x8FFFFFFFFFFFFFFF = still recording.
	int32_t first_cluster_index_with_no_audio_data; ///< First cluster index past the end of the audio data.
	uint32_t cluster_size;                          ///< Cluster::size — bytes per (non-final) cluster.
	uint32_t cluster_size_magnitude;                ///< Cluster::size_magnitude — log2(cluster_size).
	uint8_t raw_data_format;                        ///< Sample::rawDataFormat (RawDataFormat's uint8_t representation).
	uint8_t byte_depth;   ///< Sample::byteDepth — bytes per channel-sample (e.g. 2 for 16-bit).
	uint8_t num_channels; ///< Sample::numChannels — channel count (1 = mono, 2 = stereo).
} DelugeStreamingFillContext;

/// @brief Register (or replace) asset @p asset's streaming fill-context.
///
/// Called unconditionally from `SampleStream::ensure_resource_asset()` once the asset is defined
/// (and again from `open_read_stream()` if the efatfs handle becomes known afterwards) —
/// see `sample_stream.cpp`. Always compiled on the Rust/Embassy BSP regardless of the
/// `async_streaming_loader` cargo feature (asset definition happens on every BSP, not just the ones
/// with the native fill task built in); every other BSP/config links the `__attribute__((weak))`
/// no-op fallback in `async_fill.cpp`.
/// @param mgr   The resource manager instance (see deluge_streaming_resource_manager). Unused by the
///              Rust implementation today (there is exactly one process-wide manager); kept in the
///              signature for parity with the rest of the manager-scoped C ABI.
/// @param asset The asset id (`SampleStream::resource_asset_id()`).
/// @param ctx   The geometry to register for @p asset.
void deluge_streaming_set_fill_context(DelugeResource* mgr, uint32_t asset, DelugeStreamingFillContext ctx);

#ifdef __cplusplus
}
#endif
