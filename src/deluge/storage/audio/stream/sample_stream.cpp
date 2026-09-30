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

#include "storage/audio/stream/sample_stream.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h" // releaseUnleasedAudioFiles() on a full handle table
#include "storage/cluster/cluster.h"
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>

#include "deluge_resource.h"         // resource manager: a Sample is an Asset, its SAMPLE clusters the Chunks
#include "libdeluge/sample_stream.h" // the deluge_sample_stream_* registry C-ABI this facade forwards to
#include "libdeluge/streaming_fill.h" // DelugeStreamingFillContext + deluge_streaming_set_fill_context (no-slot fallback)

namespace deluge::audio::stream {

uint32_t SampleStream::resource_asset_id() const {
	return resource_asset_id_;
}

void SampleStream::set_resource_asset_id(uint32_t id) {
	resource_asset_id_ = id;
	if (stream_handle_ != 0) {
		// Write-through mirror onto the registry slot, so its own try_register_fill_context (fired by
		// this same C-ABI call, Rust-side) can register once a geometry is also present on the slot --
		// see register_fill_context()'s call sequencing, which always sets the id here BEFORE the
		// geometry that actually completes the pair.
		deluge_sample_stream_set_asset_id(stream_handle_, id);
	}
}

void SampleStream::register_fill_context() {
	// Guard first on whether the Asset is defined at all rather than on `stream_handle_`:
	// open_read_stream() calls this before deluge_streaming_define_asset() ever has on every known
	// call site (see that function's comment, chunk_residency.cpp), so gating on `stream_handle_`
	// instead would let that early call push a not-yet-parsed (pre-finalizeAfterLoad) geometry onto a
	// fresh slot; harmless in principle (registration itself still waits on a real Asset id), but a
	// needless ordering hazard to reason about. Gating on the Asset instead keeps this a true no-op
	// until deluge_streaming_define_asset()'s own call.
	if (resource_asset_id_ == DELUGE_RESOURCE_NO_ASSET) {
		return;
	}
	if (stream_handle_ != 0) {
		// Re-push the id: covers the "Asset defined while still recording, stream opened only
		// afterwards" ordering, where set_resource_asset_id()'s own write-through couldn't reach a slot
		// that didn't exist yet at definition time.
		deluge_sample_stream_set_asset_id(stream_handle_, resource_asset_id_);
		DelugeSampleStreamGeometry geo{
		    .audio_data_start_pos_bytes = sample_.audioDataStartPosBytes,
		    .audio_data_length_bytes = sample_.audioDataLengthBytes,
		    .first_cluster_index_with_no_audio_data = sample_.getFirstClusterIndexWithNoAudioData(),
		    .cluster_size = static_cast<uint32_t>(Cluster::size),
		    .cluster_size_magnitude = static_cast<uint32_t>(Cluster::size_magnitude),
		    .raw_data_format = static_cast<uint8_t>(sample_.rawDataFormat),
		    .byte_depth = static_cast<uint8_t>(sample_.byteDepth),
		    .num_channels = static_cast<uint8_t>(sample_.numChannels),
		};
		deluge_sample_stream_set_geometry(stream_handle_, geo);
		return;
	}
	// No registry slot yet -- a still-recording sample, which has nowhere in the registry to hold
	// geometry at all (opening one requires a real path). Register directly with the resource
	// manager instead: `efatfs_handle = DELUGE_HANDLE_NONE` makes any premature cluster read fail cleanly rather than
	// leaving this Asset with no fill-context registered anywhere (which would otherwise let residency
	// resolve READY with no way to ever produce real bytes).
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr == nullptr) {
		return;
	}
	DelugeStreamingFillContext ctx{
	    .efatfs_handle = DELUGE_HANDLE_NONE,
	    .audio_data_start_pos_bytes = sample_.audioDataStartPosBytes,
	    .audio_data_length_bytes = sample_.audioDataLengthBytes,
	    .first_cluster_index_with_no_audio_data = sample_.getFirstClusterIndexWithNoAudioData(),
	    .cluster_size = static_cast<uint32_t>(Cluster::size),
	    .cluster_size_magnitude = static_cast<uint32_t>(Cluster::size_magnitude),
	    .raw_data_format = static_cast<uint8_t>(sample_.rawDataFormat),
	    .byte_depth = static_cast<uint8_t>(sample_.byteDepth),
	    .num_channels = static_cast<uint8_t>(sample_.numChannels),
	};
	deluge_streaming_set_fill_context(mgr, resource_asset_id_, ctx);
}

void SampleStream::release_asset() {
	if (stream_handle_ != 0) {
		// Closes the registry slot's efatfs read handle (if any) and releases its resource-manager
		// asset mirror (kept in sync by set_resource_asset_id()/register_fill_context() above) in one
		// call. Never ALSO release resource_asset_id_ directly here -- the registry already owns that
		// release once a slot exists, and releasing twice would double-free the manager's asset slot.
		deluge_sample_stream_close(stream_handle_);
		stream_handle_ = 0;
		resource_asset_id_ = DELUGE_RESOURCE_NO_ASSET;
		return;
	}
	// No registry slot ever existed (a still-recording sample released before finishing, e.g. an
	// aborted recording) -- release the locally-cached Asset directly. The `!= NO_ASSET` guard makes
	// this idempotent, so `~SampleStream`'s backstop call after `~Sample`'s explicit one is a no-op.
	if (resource_asset_id_ != DELUGE_RESOURCE_NO_ASSET) {
		DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
		if (mgr != nullptr) {
			deluge_resource_release_asset(mgr, resource_asset_id_);
		}
		resource_asset_id_ = DELUGE_RESOURCE_NO_ASSET;
	}
}

Error SampleStream::open_read_stream(std::string_view path) {
	// efatfs IS the streaming read path — no C-FatFS fallback. Open a registry slot (which opens
	// the underlying efatfs file handle itself); a failure to open is a stream-open failure
	// propagated to the caller (the sample won't load). A still-recording sample has no read handle
	// at all (see make_read_source()) until this same path reopens one once recording finishes.
	std::string cpath{path}; // NUL-terminate for the C-ABI (path is a non-terminated string_view)
	bool table_full = false;
	uint32_t handle = deluge_sample_stream_open(cpath.c_str(), &table_full);
	if (handle == 0 && table_full) {
		// The handle table is a scarce resource with no eviction policy of its own. A
		// resident Sample pins a handle for its whole residency, and residency normally ends
		// only under MEMORY pressure — but a browser preview leaves each sample cached, and
		// samples are small, so the handle table fills long before the heap does. Without
		// this, browsing past the table's capacity made every later preview fail with
		// "Too many samples open at once" until reboot, even with most of the heap free.
		//
		// So make running out of handles trigger reclaim the way running out of memory does:
		// drop the unleased residents (their destructors close the handles) and try once
		// more. If it still fails, the streams really are all in use and the error is honest.
		if (audioFileManager.releaseUnleasedAudioFiles() > 0) {
			table_full = false;
			handle = deluge_sample_stream_open(cpath.c_str(), &table_full);
		}
	}
	if (handle == 0) {
		return table_full ? Error::TOO_MANY_OPEN_STREAMS : Error::FILE_NOT_FOUND;
	}
	stream_handle_ = handle;
	// Re-register the fill-context now the handle is known: a no-op today on every known call site
	// (open_read_stream() always runs before deluge_streaming_define_asset()'s first call — see that
	// function's comment, chunk_residency.cpp — so the handle is already registered there), but keeps
	// the table correct if a future caller ever opens the stream after the asset was already defined.
	register_fill_context();
	return Error::NONE;
}

std::unique_ptr<ReadSource> SampleStream::make_read_source() {
	// An open registry handle = the streaming read path (a normal card-loaded sample). A
	// still-recording sample (stream_handle_ == 0 -- no handle is opened until recording finishes)
	// has no reader at all. A caller that could reach a still-recording Sample must guard against it
	// itself (see WaveformRenderer::investigateWholeCluster()) -- a handle-0 read here just fails
	// cleanly (deluge_sample_stream_read_at() treats handle 0 as "no such handle").
	return std::make_unique<SampleStreamReadSource>(stream_handle_, static_cast<uint8_t>(Cluster::size_magnitude));
}

} // namespace deluge::audio::stream

extern "C" {

// The region-port open() bridge's stream-backing -> resource-asset accessor (declared in
// libdeluge/sample_stream.h alongside its sibling deluge_streaming_resource_manager): forwards to the
// asset-definition entry point (chunk_residency.cpp).
uint32_t deluge_sample_stream_asset_id(DelugeStreamBacking* stream_backing) {
	return deluge_streaming_define_asset(&deluge::audio::stream::fromAbi(stream_backing)->sample());
}

} // extern "C"
