/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"                    // Error, ClusterLoad (CLUSTER_ENQUEUE et al.)
#include "storage/audio/stream/chunk_residency.h" // deluge_streaming_define_asset() + the chunk Source callbacks
#include "storage/audio/stream/read_source.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

class Sample;

namespace deluge::audio::stream {

/// @brief Per-`Sample` orchestrator for streaming a sample's audio off the SD card in FAT-cluster-sized
///        chunks.
///
/// Every `Sample` owns exactly one `SampleStream` (as a member). It is a thin forwarding facade
/// over the `deluge_sample_stream` Rust registry (`include/libdeluge/sample_stream.h`), which
/// holds this sample's streaming state behind one opaque `stream_handle_` once a real efatfs file
/// is open:
///   - residency itself, which lives entirely in the resource manager -- there is nothing left on
///     `SampleStream` for a caller to index directly. Non-voice consumers reach it through the
///     reader C-ABI (`SampleFrameReader` / `deluge_sample_read` / `deluge_sample_peek` /
///     `deluge_sample_reserve_*` / `deluge_sample_invalidate`); the voice reaches it through the
///     region port;
///   - the open **efatfs read handle** used to pull cluster bytes off the card (the streaming read
///     path), owned by the registry slot behind `stream_handle_`;
///   - the sample's **resource-manager Asset** id: cached locally (`resource_asset_id_`, the source of
///     truth for resource_asset_id()) and write-through-mirrored onto the registry slot once
///     `stream_handle_` is real -- a still-recording Sample never opens a registry slot at all (no
///     file exists yet to open), yet still needs its Asset id cached idempotently and its (handle-
///     less) fill-context registered directly so a premature cluster read fails cleanly instead of
///     stalling with no fill-context at all; see register_fill_context() and
///     resource_asset_id()/set_resource_asset_id(). The Asset's *definition* + the `construct`
///     callback the manager invokes live in `chunk_residency.cpp` (`deluge_streaming_define_asset()`),
///     not here (eviction needs no callback — the manager frees the trivially-destructible slab chunk
///     itself);
///   - **read-source selection** — the single place a cluster read is issued from (make_read_source()).
///
/// Callers reach a cluster's bytes through the reader C-ABI or the region port (each dispatching to the
/// resource manager for residency); none branches on how a cluster's bytes are read.
///
/// @note **Real-time contract.** The audio render thread never calls into `SampleStream` per sample —
///       it reads already-resident chunk bytes by pointer from its own lookahead array. It only touches
///       this class at cluster-boundary crossings, to enqueue the next cluster via the resource manager,
///       which never blocks on I/O.
///
/// @warning Non-copyable and non-movable: it holds a back-reference to its owning `Sample` and owns the
///          read stream and Asset. This transitively makes `Sample` non-movable.
class SampleStream {
public:
	/// @param sample The owning sample; the reference is retained for this object's whole lifetime.
	explicit SampleStream(Sample& sample) : sample_{sample} {}

	SampleStream(const SampleStream&) = delete;
	SampleStream& operator=(const SampleStream&) = delete;
	SampleStream(SampleStream&&) = delete;
	SampleStream& operator=(SampleStream&&) = delete;

	/// Releases the Asset as a defensive backstop; a no-op on the normal path, where `~Sample` has
	/// already released it (see release_asset() for why that explicit, earlier release is required).
	~SampleStream() { release_asset(); }

	/// @return The owning `Sample` (the back-reference this stream was constructed with). Used by the
	///         region-port cursor bridge (`deluge_sample_stream_asset_id()`, sample_stream.cpp) to reach
	///         `deluge_streaming_define_asset()`, which takes a `Sample*` rather than a `SampleStream*`.
	[[nodiscard]] Sample& sample() { return sample_; }

	/// @name Resource-manager Asset lifecycle
	/// @{

	/// @return This sample's Asset id, or DELUGE_RESOURCE_NO_ASSET if not yet defined. Always reads
	///         the local cache (`resource_asset_id_`) -- see the class doc for why that, not the
	///         registry slot, stays the source of truth for this getter.
	[[nodiscard]] uint32_t resource_asset_id() const;

	/// @brief Set this sample's cached Asset id. Storage only -- the asset-*definition* logic that
	///        assigns it lives in `deluge_streaming_define_asset()` (chunk_residency.cpp), which
	///        reads/writes it via this setter and the getter above (through `sample->stream()`).
	///        Writes the local cache unconditionally, and mirrors the id onto the registry slot too
	///        (write-through) once `stream_handle_` is real -- see the class doc.
	/// @param id The Asset id to cache.
	void set_resource_asset_id(uint32_t id);

	/// @brief Release the Asset, freeing every resident cluster's backing first.
	///
	/// Releasing frees the manager's slab slot for each resident Chunk directly — there is no evict
	/// callback (the streamed chunk is a trivially-destructible POD in the slab). Idempotent (a no-op if
	/// the Asset was never defined or is already released).
	/// @warning Release before the `Sample`/`SampleStream` is destroyed so the manager frees this
	///          asset's resident backings rather than orphaning them; `~Sample` calls it explicitly,
	///          before any `Sample` member is destructed, rather than leaving it to `~SampleStream`
	///          alone.
	void release_asset();

	/// @}
	/// @name Read stream
	/// @{

	/// @brief Open the streaming-registry slot (and its efatfs read handle) used for every subsequent
	///        cluster read.
	///
	/// Opens the slot once (typically from `AudioFileManager::buildAudioFileFromCard`) for the rest of
	/// the sample's life. efatfs is the streaming read path outright — there is no C-FatFS fallback.
	/// @param path Path to open.
	/// @return `Error::NONE` on success, leaving the handle engaged; otherwise the stream is left
	///         disengaged and the failure reason is distinguished: `Error::TOO_MANY_OPEN_STREAMS` if
	///         the streaming-registry's slot table (or the underlying efatfs handle table) has no free
	///         slot (a real file being silently dropped, not a missing one), `Error::FILE_NOT_FOUND` for
	///         every other open failure (bad path, unmounted FS).
	Error open_read_stream(std::string_view path);

	/// @brief Build this sample's read source.
	///
	/// The single point a cluster read is issued from; no caller branches on it.
	/// @return A `SampleStreamReadSource` over the registry handle (0 if none is open yet -- e.g. a
	///         still-recording sample, which has no reader at all; see the .cpp for why a handle-0
	///         read is safe and simply fails).
	[[nodiscard]] std::unique_ptr<ReadSource> make_read_source();

	/// @}

	/// @brief Register (or refresh) this asset's streaming fill-context with the resource manager.
	///
	/// A no-op if the Asset isn't defined yet (`resource_asset_id_ == DELUGE_RESOURCE_NO_ASSET`) --
	/// gated on the Asset, not on `stream_handle_`, so open_read_stream()'s own call (which always runs
	/// before deluge_streaming_define_asset()'s first call -- see that function's comment,
	/// chunk_residency.cpp) stays a true no-op. Called again from `deluge_streaming_define_asset()`
	/// right after the Asset is defined (see their call sites for why both are needed). Once
	/// `stream_handle_` is real, this forwards to the registry (`deluge_sample_stream_set_geometry`,
	/// re-pushing the Asset id onto the slot first in case `open_read_stream()` raced ahead of a
	/// not-yet-defined Asset). Before that -- a still-recording Sample, with no registry slot to hold
	/// geometry at all -- it registers directly with the resource manager
	/// (`deluge_streaming_set_fill_context`, `efatfs_handle = 0`), so a premature cluster read fails
	/// cleanly instead of a still-recording Asset having no fill-context registered anywhere. Kept as a
	/// `SampleStream` method (not relocated alongside the asset-definition core) because it is
	/// stream/geometry-coupled -- it reads `sample_`'s geometry and `stream_handle_`/`resource_asset_id_`
	/// directly -- and open_read_stream() needs to call it too.
	void register_fill_context();

private:
	Sample& sample_;

	/// This sample's Asset id, DELUGE_RESOURCE_NO_ASSET until defined on first use. The source of
	/// truth for resource_asset_id()/set_resource_asset_id() regardless of `stream_handle_` state --
	/// set_resource_asset_id() also write-through-mirrors it onto the registry slot once one exists,
	/// purely so the registry can fire its own fill-context registration once a geometry is also
	/// present (see register_fill_context()). The sentinel is a literal (rather than including
	/// `deluge_resource.h`) because this header is pulled in transitively by every includer of
	/// `sample.h`.
	uint32_t resource_asset_id_ = 0xFFFFFFFFu;

	/// This stream's `deluge_sample_stream` registry slot handle (0 = none open). Defaults to 0 so a
	/// still-recording Sample (no handle opened until recording finishes) is unaffected;
	/// open_read_stream() sets it via deluge_sample_stream_open() and release_asset() clears it.
	uint32_t stream_handle_ = 0;
};

} // namespace deluge::audio::stream
