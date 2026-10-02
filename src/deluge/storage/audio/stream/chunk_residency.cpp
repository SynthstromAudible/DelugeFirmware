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

#include "storage/audio/stream/chunk_residency.h"

#include "memory/general_memory_allocator.h"
#include "model/sample/sample.h"

#include "deluge_resource.h" // resource manager: a Sample is an Asset, its SAMPLE clusters the Chunks

// Resource-manager Source callbacks (contract documented in sample_stream.h and chunk_residency.h).
// `owner` is the Sample* registered by deluge_streaming_define_asset() below. The chunk IS the
// manager's slab backing (placement-new'd at `dest`); residency lives entirely in the manager, and
// these callbacks do not mirror anything into SampleStream.

uint32_t deluge_streaming_define_asset(Sample* sample) {
	deluge::audio::stream::SampleStream& stream = sample->stream();
	if (stream.resource_asset_id() != DELUGE_RESOURCE_NO_ASSET) {
		return stream.resource_asset_id();
	}
	// The manager is the sole SDRAM evictor now, so every Sample (playback or recording) is
	// manager-owned. A missing manager / full asset table is fatal — no legacy fallback.
	// Cost reflects rebuild expense: a converted sample (float / wrong-endian) costs a read PLUS a
	// format re-conversion, so it's kept resident longer than a native one (one plain read).
	uint32_t clusterCost =
	    (sample->rawDataFormat != RawDataFormat::NATIVE) ? DELUGE_RESOURCE_COST_IO_CONVERTED : DELUGE_RESOURCE_COST_IO;
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	uint32_t asset_id = (mgr != nullptr) ? deluge_resource_define_asset(mgr, sample, nullptr /*materialize: never*/,
	                                                                    /*on_evict=*/nullptr, nullptr, clusterCost,
	                                                                    DELUGE_RESOURCE_BACKING_SLAB)
	                                     : DELUGE_RESOURCE_NO_ASSET;
	stream.set_resource_asset_id(asset_id);
	if (stream.resource_asset_id() == DELUGE_RESOURCE_NO_ASSET) {
		FREEZE_WITH_ERROR("RSA1"); // resource asset table exhausted (raise kAssetCap)
	}
	// Attach the async-prefetch path so CLUSTER_ENQUEUE can request (construct now, load later).
	deluge_resource_set_construct(mgr, stream.resource_asset_id(), deluge_streaming_chunk_construct);
	// If the sample is already project-relevant (a holder gained it before its first stream), apply the
	// soft-reference now — numReasonsIncreasedFromZero fired before the asset existed, so it was a no-op.
	if (sample->isProjectReferenced()) {
		deluge_resource_reference(mgr, stream.resource_asset_id());
	}
	// Register this asset's fill-context. Ordering: open_read_stream() is always
	// called before this point on the only path that ever assigns an efatfs handle
	// (AudioFileManager::buildAudioFileFromCard opens the stream, then loadFile()'s cluster reads
	// trigger this function on first use) — so efatfs_handle_ is already whatever it will be for this
	// stream's life (a real handle for a card-loaded sample, still 0 for a sample under construction
	// by the recorder, which never opens one). register_fill_context() is called again from
	// open_read_stream() as a defensive re-registration, in case that ordering ever changes.
	stream.register_fill_context();
	return stream.resource_asset_id();
}

// The chunk-construct callback (`deluge_streaming_chunk_construct`, registered above) and every
// chunk field accessor live in Rust (`deluge_sample_fill::chunk`) — the streamed chunk's storage
// lives there, so this TU only *registers* the Rust construct symbol; it does not define it or
// touch the chunk's byte layout.

// No evict callback: the streamed chunk is a trivially-destructible POD living in the
// manager's slab, and the manager frees the slab + auto-de-queues the loader entry on eviction —
// there is nothing an evict callback would need to do, so the asset registers a null on_evict.
