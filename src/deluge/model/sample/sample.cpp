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

#include "model/sample/sample.h"
#include "NE10.h"
#include "definitions_cxx.hpp"
#include "dsp/fft/fft_config_manager.h"
#include "dsp/timestretch/time_stretcher.h"
#include "io/debug/log.h"
#include "libdeluge/sample_reader.h"
#include "memory/general_memory_allocator.h"
#include "model/sample/pitch_scan.h"
#include "model/sample/sample_cache.h"
#include "model/sample/sample_perc_cache_zone.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h" // audioFileManager (overviewScanAllDone)
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"
#include <cmath>
#include <cstring>
#include <new>

#include "deluge_resource.h" // resource manager: a Sample is an Asset, its SAMPLE clusters the Chunks

namespace {
// While a Sample's length is still unknown (mid-recording), its cluster count is the number of clusters
// the recorder has captured so far. Mirrors the unsynchronized walk in
// WaveformRenderer::investigateWholeCluster (waveform_renderer.cpp).
//
// PRECONDITION: this unsynchronized list walk is safe only because num_clusters() reaches it just for
// still-recording samples (isLengthKnown() == false), which are never consumed by the preemptive
// streaming/region-port playback path (a recording target never opens a read stream). Its callers run
// in the same cooperative/UI/diagnostic context as firstRecorder's structural mutation. If a future
// change lets num_clusters() run on a recording sample from a truly preemptive context, this walk would
// need synchronization against the card-routine add/remove.
size_t liveRecorderClusterCount(const Sample& sample) {
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder != nullptr; recorder = recorder->next) {
		if (recorder->sample == &sample) {
			int32_t index = recorder->currentRecordClusterIndex.load(std::memory_order_acquire);
			return index < 0 ? 0 : static_cast<size_t>(index);
		}
	}
	return 0;
}
} // namespace

#if SAMPLE_DO_LOCKS
#define LOCK_ENTRY                                                                                                     \
	if (lock) {                                                                                                        \
		FREEZE_WITH_ERROR("i024");                                                                                     \
	}                                                                                                                  \
	lock = true;
#define LOCK_EXIT lock = false;
#else
#define LOCK_ENTRY                                                                                                     \
	{}
#define LOCK_EXIT                                                                                                      \
	{}
#endif

// Comparison offsets for searchPercCacheZones(), formerly provided by the now-deleted ResizeableArray
// hierarchy: GREATER_OR_EQUAL yields the lower bound itself, LESS the element just before it.
constexpr int32_t GREATER_OR_EQUAL = 0;
constexpr int32_t LESS = -1;

// Mirrors OrderedResizeableArray::search(): returns the index of the first zone whose startPos >= key,
// plus `comparison` (GREATER_OR_EQUAL = 0 or LESS = -1).
static int32_t searchPercCacheZones(deluge::vector<SamplePercCacheZone> const& zones, int32_t key, int32_t comparison) {
	auto it = std::lower_bound(zones.begin(), zones.end(), key,
	                           [](SamplePercCacheZone const& zone, int32_t k) { return zone.startPos < k; });
	return static_cast<int32_t>(it - zones.begin()) + comparison;
}

Sample::Sample() : AudioFile(AudioFileType::SAMPLE) {
	audioDataLengthBytes = 0;
	audioDataStartPosBytes = 0;
	lengthInSamples = 0;
	rawDataFormat = RawDataFormat::NATIVE;
	midiNote = MIDI_NOTE_UNSET;
	partOfFolderBeingLoaded = false;

	minValueFound = 2147483647;
	maxValueFound = -2147483648;

	percCacheMemory[0] = nullptr;
	percCacheMemory[1] = nullptr;

	percCacheClusters[0] = nullptr;
	percCacheClusters[1] = nullptr;

	fileLoopStartSamples = 0;
	fileLoopEndSamples = 0;
	midiNoteFromFile = -1;

	beginningOffsetForPitchDetection = 0;
	beginningOffsetForPitchDetectionFound = false;

#if SAMPLE_DO_LOCKS
	lock = false;
#endif
}

Error Sample::initialize(int32_t newNumClusters) {
	unloadable = false;
	unplayable = false;
	waveTableCycleSize = 2048; // Default
	fileExplicitlySpecifiesSelfAsWaveTable = false;

	try {
		overviewCache_.resize(overviewCache_.size() + newNumClusters);
	} catch (deluge::exception&) {
		return Error::INSUFFICIENT_RAM;
	}
	return Error::NONE;
}

// The SAMPLE-cluster resource-manager Source's construct callback (materialize and evict are both
// null -- eviction needs no callback; a StreamedChunk is a trivially-destructible slab POD) and the
// asset-definition entry deluge_streaming_define_asset() live in
// storage/audio/stream/chunk_residency.cpp; the Asset id cache lives on
// deluge::audio::stream::SampleStream -- storage/audio/stream/sample_stream.{h,cpp}.

// === Resource-manager Source for the perc cache (per play-direction) =========
// Perc clusters are written incrementally by the time-stretcher (no materialize), and
// leased-while-nearby (TimeStretcher deluge::cluster::add_lease/remove_reason via resource_lease_asset_id).
// ctx carries the play-direction (0=forwards, 1=reversed). Per-cluster independent (no
// tail-first); self_protect so the fill can't evict its own just-written cluster.
static void percCacheConstruct(void* ctx, void* owner, uint32_t index, void* dest) {
	auto* sample = static_cast<Sample*>(owner);
	int32_t reversed = static_cast<int32_t>(reinterpret_cast<intptr_t>(ctx));
	auto* cluster = new (dest) ComputedChunk();
	cluster->payload_ = reinterpret_cast<std::byte*>(dest) + kChunkPayloadOffset; // slot-provenance payload
	cluster->type = reversed ? Cluster::Type::PERC_CACHE_REVERSED : Cluster::Type::PERC_CACHE_FORWARDS;
	cluster->sample = sample;
	cluster->cluster_index = index;
	cluster->resource_slot = deluge_resource_slot_of(GeneralMemoryAllocator::get().resourceManager(), dest);
	sample->percCacheClusters[reversed][index] = cluster;
}

static void percCacheEvict(void* ctx, void* owner, uint32_t index) {
	auto* sample = static_cast<Sample*>(owner);
	int32_t reversed = static_cast<int32_t>(reinterpret_cast<intptr_t>(ctx));
	ComputedChunk* cluster = sample->percCacheClusters[reversed][index];
	if (cluster != nullptr) {
		sample->percCacheClusterStolen(cluster); // nulls percCacheClusters[reversed][index] + trims zones
		cluster->~ComputedChunk();               // manager frees the slab slot
	}
}

Sample::~Sample() {
	// stream_'s read-stream handle (a std::optional<deluge::io::Stream>) closes automatically when
	// stream_ destructs below (a disengaged optional does nothing; an engaged one destructs its
	// Stream, closing it).

	// Retire our Asset first so the manager frees every backing this sample still has resident
	// (directly -- there is no evict callback; a StreamedChunk is a trivially-destructible POD in the
	// manager's slab). No-op if we never defined one. This explicit, early call is the clean ordering
	// (see sample_stream.h's release_asset() doc); ~SampleStream() also calls it, as an idempotent
	// backstop, once stream_ destructs below.
	stream_.release_asset();

	deletePercCache(true);

	for (SampleCacheElement& element : caches) {
		element.cache->~SampleCache();
		delugeDealloc(element.cache);
	}
}

void Sample::deletePercCache(bool beingDestructed) {

	for (int32_t reversed = 0; reversed < 2; reversed++) {
		if (percCacheMemory[reversed]) {
			delugeDealloc(percCacheMemory[reversed]);
			if (!beingDestructed) {
				percCacheMemory[reversed] = nullptr;
			}
		}

		// Retire the Asset — the manager frees all this direction's resident perc clusters (via
		// percCacheEvict, which nulls percCacheClusters[reversed][c] as it goes). NO_ASSET here just
		// means this direction's cache was never filled.
		if (percCacheAssetId[reversed] != DELUGE_RESOURCE_NO_ASSET) {
			DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
			if (mgr != nullptr) {
				deluge_resource_release_asset(mgr, percCacheAssetId[reversed]);
			}
			percCacheAssetId[reversed] = DELUGE_RESOURCE_NO_ASSET;
		}

		if (percCacheClusters[reversed]) {
			// The Asset retire above has already freed + nulled every resident perc cluster, so
			// there's nothing to destruct here — just free the pointer array.
			delugeDealloc(percCacheClusters[reversed]);
			if (!beingDestructed) {
				percCacheClusters[reversed] = nullptr;
			}
		}

		if (!beingDestructed) {
			percCacheZones[reversed].clear();
		}
	}
}

size_t Sample::num_clusters() const {
	return isLengthKnown() ? geometricClusterCount() : liveRecorderClusterCount(*this);
}

void Sample::workOutBitMask() {
	bitMask = 0xFFFFFFFF << ((4 - byteDepth) * 8);
}

void Sample::markAsUnloadable() {
	unloadable = true;

	// The on-disk audio may have changed, so the cached waveform overview can no longer be trusted.
	resetOverviewScan();

	// Cancel any pending loads and flag resident clusters, so a mid-flight fill won't complete with
	// stale bytes (the residency lives in the Rust manager now).
	deluge_sample_invalidate(deluge::sample::source_id_for(*this));
}

void Sample::resetOverviewScan() {
	for (int32_t c = 0; c < static_cast<int32_t>(overviewCacheSize()); c++) {
		OverviewCacheEntry& cacheEntry = overviewCacheEntry(c);
		cacheEntry.investigated = false;
		cacheEntry.min = 127;
		cacheEntry.max = -128;
	}
	overviewScanNextCluster = getFirstClusterIndexWithAudioData();
	audioFileManager.overviewScanAllDone = false; // This sample now has work to pre-scan again (#4460)
}

SampleCache* Sample::getOrCreateCache(SampleHolder* sampleHolder, int32_t phaseIncrement, int32_t timeStretchRatio,
                                      bool reversed, bool mayCreate, bool* created) {

	int32_t skipSamplesAtStart;
	if (!reversed) {
		skipSamplesAtStart = sampleHolder->startPos;
	}
	else {
		skipSamplesAtStart = lengthInSamples - sampleHolder->getEndPos(false);
	}

	std::array<uint32_t, 4> keyWords = {static_cast<uint32_t>(phaseIncrement), static_cast<uint32_t>(timeStretchRatio),
	                                    static_cast<uint32_t>(skipSamplesAtStart), static_cast<uint32_t>(reversed)};
	auto cacheKeyLess = [](SampleCacheElement const& element, std::array<uint32_t, 4> const& key) {
		return element.key() < key;
	};
	auto it = std::lower_bound(caches.begin(), caches.end(), keyWords, cacheKeyLess);

	// If it already existed...
	if (it != caches.end() && it->key() == keyWords) {
		*created = false;
		return it->cache;
	}

	// Or if still here, it didn't already exist.
	if (!mayCreate) {
		return nullptr;
	}

	uint64_t combinedIncrement = ((uint64_t)(uint32_t)phaseIncrement * (uint32_t)timeStretchRatio) >> 24;

	uint64_t lengthInSamplesCached = ((uint64_t)(lengthInSamples - skipSamplesAtStart) << 24) / combinedIncrement
	                                 + 1; // Not 100% sure on the +1, but better safe than sorry

	// Make it a bit longer, to capture the ring-out of the interpolation / time-stretching
	if (phaseIncrement != kMaxSampleValue) {
		lengthInSamplesCached += (kInterpolationMaxNumSamples >> 1);
	}
	if (timeStretchRatio != kMaxSampleValue) {
		lengthInSamplesCached += 16384; // This one is quite an inexact science
	}

	uint64_t lengthInBytesCached = lengthInSamplesCached * kCacheByteDepth * numChannels;

	if (lengthInBytesCached >= (32 << 20)) {
		return nullptr; // If cache would be more than 32MB, assume that it wouldn't be very useful to cache it
	}

	int32_t numClusters = ((lengthInBytesCached - 1) >> Cluster::size_magnitude) + 1;

	void* memory = deluge::memory::alloc_sdram(sizeof(SampleCache) + (numClusters - 1) * sizeof(ComputedChunk*));
	if (memory == nullptr) {
		return nullptr;
	}

	auto* samplePitchAdjustment = new (memory) SampleCache(this, numClusters, lengthInBytesCached, phaseIncrement,
	                                                       timeStretchRatio, skipSamplesAtStart, reversed);

	try {
		// Re-search: the cache-memory allocation above may have run other code, so don't trust the old iterator.
		it = std::lower_bound(caches.begin(), caches.end(), keyWords, cacheKeyLess);
		caches.insert(it, SampleCacheElement{phaseIncrement, timeStretchRatio, skipSamplesAtStart,
		                                     static_cast<bool>(reversed), samplePitchAdjustment});
	} catch (deluge::exception&) {
		samplePitchAdjustment->~SampleCache();
		delugeDealloc(memory);
		return nullptr;
	}

	*created = true;
	return samplePitchAdjustment;
}

void Sample::deleteCache(SampleCache* cache) {
	// Not currently used anymore
	/*
	int32_t i = pitchAdjustmentCaches.search(cache->phaseIncrement, cache->skipSamplesAtStart, cache->reversed,
	GREATER_OR_EQUAL); if (i < pitchAdjustmentCaches.getNumElements()) { SampleCacheElement* element =
	pitchAdjustmentCaches.getElement(i);

	    if (element->phaseIncrement != cache->phaseIncrement
	            || element->skipSamplesAtStart != cache->skipSamplesAtStart
	            || element->reversed != cache->reversed) return;

	    cache->~SampleCache();
	    pitchAdjustmentCaches.deleteElement(i);
	    D_PRINTLN("cache deleted");
	}
	*/
}

#define MEASURE_PERC_CACHE_PERFORMANCE 0

// Returns error
Error Sample::fillPercCache(TimeStretcher* timeStretcher, int32_t startPosSamples, int32_t endPosSamples,
                            int32_t playDirection, int32_t maxNumSamplesToProcess) {

#if MEASURE_PERC_CACHE_PERFORMANCE
	uint16_t startTime = MTU2.TCNT_0;
#endif

	auto reversed = static_cast<int32_t>(playDirection != 1);
	// If the start pos is already beyond the waveform, we can get out right now!
	if (!reversed) {
		if (startPosSamples >= lengthInSamples) {
			return Error::NONE;
		}
	}
	else {
		if (startPosSamples < 0) {
			return Error::NONE;
		}
	}

	LOCK_ENTRY

	AudioEngine::logAction("fillPercCache");

	// int32_t lengthInSamplesAfterReduction = ((lengthInSamples + (kPercBufferReductionSize >> 1)) >>
	// PERC_BUFFER_REDUCTION_MAGNITUDE);
	int32_t lengthInSamplesAfterReduction = ((lengthInSamples - 1) >> kPercBufferReductionMagnitude) + 1;
	lengthInSamplesAfterReduction = std::max(lengthInSamplesAfterReduction, 1_i32); // Can't allocate less than 1 byte

	bool percCacheDoneWithClusters = (lengthInSamplesAfterReduction >= (Cluster::size >> 1));

	if (percCacheDoneWithClusters) {
		if (!percCacheClusters[reversed]) {
			numPercCacheClusters = ((lengthInSamplesAfterReduction - 1) >> Cluster::size_magnitude)
			                       + 1; // Stores this number for the future too
			int32_t memorySize = numPercCacheClusters * sizeof(ComputedChunk*);
			percCacheClusters[reversed] = (ComputedChunk**)deluge::memory::alloc_fast(memorySize);
			if (!percCacheClusters[reversed]) {
				LOCK_EXIT
				return Error::INSUFFICIENT_RAM;
			}

			memset(percCacheClusters[reversed], 0, memorySize);

			// Define this direction's perc-cache Asset (construct-only; the manager owns residency
			// + eviction for its clusters). The manager is the sole SDRAM evictor, so a missing
			// manager / full asset table is fatal (no legacy fallback) — size the asset cap for it.
			DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
			percCacheAssetId[reversed] =
			    (mgr != nullptr)
			        ? deluge_resource_define_asset(mgr, this, nullptr /*materialize: never*/, percCacheEvict,
			                                       reinterpret_cast<void*>(static_cast<intptr_t>(reversed)),
			                                       DELUGE_RESOURCE_COST_CPU_PERC, DELUGE_RESOURCE_BACKING_SLAB)
			        : DELUGE_RESOURCE_NO_ASSET;
			if (percCacheAssetId[reversed] == DELUGE_RESOURCE_NO_ASSET) {
				FREEZE_WITH_ERROR("RPC1"); // resource asset table exhausted (raise kAssetCap)
			}
			deluge_resource_set_construct(mgr, percCacheAssetId[reversed], percCacheConstruct);
			deluge_resource_set_self_protect(mgr, percCacheAssetId[reversed], true);
			// per-cluster independent (percCacheClusterStolen trims one slot) → NOT evict_tail_first
			// Inherit this sample's project relevance (applyProjectReference toggles it thereafter).
			if (isProjectReferenced()) {
				deluge_resource_reference(mgr, percCacheAssetId[reversed]);
			}
		}
	}

	else {

		if (!percCacheMemory[reversed]) {
			int32_t percCacheSize = lengthInSamplesAfterReduction;

			percCacheMemory[reversed] = (uint8_t*)deluge::memory::alloc_sdram(percCacheSize);
			if (!percCacheMemory[reversed]) {
				LOCK_EXIT
				return Error::INSUFFICIENT_RAM;
			}

			// D_PRINTLN("allocated percCacheMemory");
		}
	}

	int32_t bytesPerSample = numChannels * byteDepth;
	int32_t posIncrement = bytesPerSample * playDirection;

	const uint32_t sourceId = deluge::sample::source_id_for(*this);

	int32_t i;
	if (!reversed) {
		i = searchPercCacheZones(percCacheZones[reversed], startPosSamples + 1, LESS);
	}
	else {
		i = searchPercCacheZones(percCacheZones[reversed], startPosSamples, GREATER_OR_EQUAL);
	}

	Error error = Error::NONE;
	SamplePercCacheZone* percCacheZone;
	if (i >= 0 && i < static_cast<int32_t>(percCacheZones[reversed].size())) {
		percCacheZone = &percCacheZones[reversed][i];

		// Primarily, we check here whether this zone ends after our start-pos. However, we also test positive if the
		// zone's end is *almost* as far along as our start-pos but not quite. In such a case, it still makes sense to
		// continue adding to that zone, starting a little further back than we had planned to. This prevents the
		// situation where time-stretching is on extremely fast and each call to this function is so much further along
		// that a new zone is created every time, leading to thousands of zones, so huge overhead each time we want to
		// insert or delete. Instead, this new method will cause the zones to clump together, or better yet just manage
		// to cover the whole area in one zone. This is far more efficient in every way - remember that each zone will
		// have a number of samplesAtStartWhichShouldBeReplaced, so ending up with thousands of zones is just a terrible
		// idea.
		if ((percCacheZone->endPos - startPosSamples) * playDirection
		    >= -2048) { // -2048 helps massively. Not sure if we can go lower. Also tested -4096 - same result. Not
			            // fine-tuned beyond that

			// Reset startPosSamples back to the zone endPos, which may have been a bit further back. That's the place
			// where we're guaranteed that there's still a perc cache Cluster (I think? Or unless it's the first sample
			// of a new one?)
			startPosSamples = percCacheZone->endPos; // This can end up as -1! Because endPos can - see its comment.

			// If the (potentially made-later) start pos is already beyond the waveform, get out (otherwise we'd be
			// prone to an error getting the perc Cluster below. Fixed Aug 2021
			if (!reversed) {
				if (startPosSamples >= lengthInSamples) {
doReturnNoError:
					LOCK_EXIT
					return Error::NONE;
				}
			}
			else {
				if (startPosSamples < 0) {
					goto doReturnNoError;
				}
			}

			// First, update our "current pos for perc cache filling and reading" sorta thing so
			// no one steals the first Cluster we're gonna need. This is especially important for just now while we're
			// gonna be reading some of this Cluster, but also we want to keep it in memory for next time we come back
			// here.
			int32_t percClusterIndexStart;
			if (percCacheDoneWithClusters) {
				percClusterIndexStart =
				    (uint32_t)startPosSamples >> (Cluster::size_magnitude + kPercBufferReductionMagnitude);
				if (ALPHA_OR_BETA_VERSION && percClusterIndexStart >= numPercCacheClusters) {
					FREEZE_WITH_ERROR("E138");
				}
				ComputedChunk* clusterHere = percCacheClusters[reversed][percClusterIndexStart];
#if ALPHA_OR_BETA_VERSION
				if (!clusterHere) {

					// That's actually allowed if we're right at the start of that cluster. But otherwise...
					if (startPosSamples & ((1 << (Cluster::size_magnitude + kPercBufferReductionMagnitude)) - 1)) {
						// If Cluster has been stolen, the zones should have been updated, so we shouldn't be here
						D_PRINTLN("startPosSamples: %d", startPosSamples);
						FREEZE_WITH_ERROR("E139");
					}
				}
#endif
				if (clusterHere) {
					timeStretcher->rememberPercCacheCluster(
					    clusterHere); // If at start of new cluster, there might not be one allocated here yet
				}
			}

			// If it ends after our end-pos too, we're done
			if ((percCacheZone->endPos - endPosSamples) * playDirection >= 0) {

				// But first, if our perc cache is done with Clusters, see if our endPos has a different perc cache
				// Cluster than our startPos, and if so, store it. (It won't be more than 1 Cluster ahead, cos remember
				// the data is so compacted that each perc cache Cluster stores like 90 seconds.)
				if (percCacheDoneWithClusters) {

					// I think the fact that we subtract playDirection here means that we look at the cluster for the
					// very last existing sample, so even if we've actually filled up right up to the cluster boundary
					// but not allocated a next one, it should be fine, ya know?
					int32_t percClusterIndexEnd = (uint32_t)(endPosSamples - playDirection)
					                              >> (Cluster::size_magnitude + kPercBufferReductionMagnitude);
					if (percClusterIndexEnd != percClusterIndexStart) {
#if ALPHA_OR_BETA_VERSION
						if (percClusterIndexEnd >= numPercCacheClusters) {
							FREEZE_WITH_ERROR("E140");
						}
						if (!percCacheClusters[reversed][percClusterIndexEnd]) {
							// If Cluster has been stolen, the zones should have been updated, so we shouldn't be here
							FREEZE_WITH_ERROR("E141");
						}
#endif
						timeStretcher->rememberPercCacheCluster(percCacheClusters[reversed][percClusterIndexEnd]);
					}
				}

				// We're now guaranteed to have a bunch of perc cache secured in RAM, un-stealable. So we can take a
				// breather and know we won't need access to the source Clusters for it anytime very soon
				timeStretcher->unassignAllReasonsForPercLookahead();

				goto doReturnNoError;
			}

			// Or if it ends before our end-pos, we need to add to it
			else {
				goto doLoading;
			}
		}
	}

	// If still here, need to create element. And we know that perc cache Clusters will be allocated and remembered if
	// necessary
	if (!reversed) {
		i++;
	}

	// The zone arrays live on the external (non-stealable) region, so this growth can never steal this
	// Sample's own perc cache Clusters and re-enter the array. There's also still a lock to alert if
	// reentrant modification somehow happens anyway.
	try {
		percCacheZones[reversed].insert(percCacheZones[reversed].begin() + i, SamplePercCacheZone(startPosSamples));
	} catch (deluge::exception&) {
		LOCK_EXIT
		return Error::INSUFFICIENT_RAM;
	}

	percCacheZone = &percCacheZones[reversed][i];

doLoading:

#if MEASURE_PERC_CACHE_PERFORMANCE
	int32_t length = (endPosSamples - startPosSamples) * playDirection;
	if (length < kPercBufferReductionSize * 16)
		goto doReturnResult;
	else
		endPosSamples = startPosSamples + kPercBufferReductionSize * 16 * playDirection;
#endif

	// Make sure we don't shoot past end of waveform
	if (!reversed) {
		endPosSamples = std::min(endPosSamples, (int32_t)lengthInSamples);
	}
	else {
		endPosSamples = std::max(endPosSamples, (int32_t)-1);
	}

#if !MEASURE_PERC_CACHE_PERFORMANCE
	int32_t endPosSamplesLimit0 = startPosSamples + maxNumSamplesToProcess * playDirection;
	if ((endPosSamples - endPosSamplesLimit0) * playDirection >= 0) {
		endPosSamples = endPosSamplesLimit0;
	}
#endif

	// See if there's a next element which we should stop before
	int32_t iNext = i + playDirection;
	bool willHitNextElement = false;
	int32_t endPosSamplesLimit;
	SamplePercCacheZone* nextPercCacheZone;
	if (iNext >= 0 && iNext < static_cast<int32_t>(percCacheZones[reversed].size())) {
		nextPercCacheZone = &percCacheZones[reversed][iNext];
		if ((endPosSamples - nextPercCacheZone->startPos) * playDirection >= 0) {
			willHitNextElement = true;

			endPosSamplesLimit =
			    nextPercCacheZone->startPos + nextPercCacheZone->samplesAtStartWhichShouldBeReplaced * playDirection;

			if ((endPosSamples - endPosSamplesLimit) * playDirection >= 0) {
				// TODO: what if that next zone doesn't extend all the way to the end we want?
				endPosSamples = endPosSamplesLimit;
			}
		}
	}

	int32_t sourceBytePos;
	int32_t numSamples = (endPosSamples - startPosSamples) * playDirection;
	if (numSamples <= 0) {
		goto getOut; // This probably would have already been dealt with above - not quite sure
	}
	sourceBytePos = audioDataStartPosBytes + startPosSamples * bytesPerSample;

	do {

		int32_t numSamplesThisClusterReadWrite = numSamples;

		int32_t sourceClusterIndex = sourceBytePos >> Cluster::size_magnitude;

		if (sourceClusterIndex >= getFirstClusterIndexWithNoAudioData()
		    || sourceClusterIndex
		           < getFirstClusterIndexWithAudioData()) { // Wait, this shouldn't actually happen right?
			goto getOut;
		}

		uint8_t* percCacheNow;
		if (percCacheDoneWithClusters) {
			int32_t percClusterIndex = startPosSamples >> (Cluster::size_magnitude + kPercBufferReductionMagnitude);
			if (ALPHA_OR_BETA_VERSION && percClusterIndex >= numPercCacheClusters) {
				FREEZE_WITH_ERROR("E136");
			}
			if (!percCacheClusters[reversed][percClusterIndex]) {
				// D_PRINTLN("allocating perc cache Cluster!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
				//  We tell it not to steal any other per cache Cluster from this Sample - not because those Clusters
				//  are definitely a high priority to keep, but because doing so would probably alter our
				//  percCacheZones, which we're currently working with, which could really muck things up. Scenario only
				//  discovered Jan 2021. (Manager path: the asset's self_protect provides the same guarantee.)
				// Manager-owned: request constructs the Cluster (percCacheConstruct sets
				// type/sample/index + percCacheClusters[reversed][index]) + leases; release so it's
				// resident-but-unleased (the TimeStretcher re-leases the nearby ones via
				// deluge::cluster::add_lease).
				DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
				void* p =
				    deluge_resource_request(mgr, percCacheAssetId[reversed], percClusterIndex, kSlabBackedSizeIgnored);
				if (p == nullptr) {
					error = Error::INSUFFICIENT_RAM;
					goto getOut;
				}
				deluge_resource_release(mgr, p);
			}

			timeStretcher->rememberPercCacheCluster(percCacheClusters[reversed][percClusterIndex]);

			percCacheNow = reinterpret_cast<uint8_t*>(percCacheClusters[reversed][percClusterIndex]->payload().data())
			               - (percClusterIndex * Cluster::size);

			int32_t posWithinPercClusterBig = startPosSamples & ((Cluster::size << kPercBufferReductionMagnitude) - 1);

			// Bytes and samples are the same for the dest Cluster
			int32_t samplesLeftThisDestCluster =
			    reversed ? (posWithinPercClusterBig + 1)
			             : ((Cluster::size << kPercBufferReductionMagnitude) - posWithinPercClusterBig);
			numSamplesThisClusterReadWrite = std::min(numSamplesThisClusterReadWrite, samplesLeftThisDestCluster);
		}

		else {
			percCacheNow = percCacheMemory[reversed];
		}

		// Don't call getCluster() - that would add a reason, and potentially do loading and stuff. This
		// peek is genuinely zero-lease.
		const uint64_t frame = (uint32_t)(sourceBytePos - audioDataStartPosBytes) / (uint8_t)bytesPerSample;
		const DelugeFrameWindow window = deluge_sample_peek(sourceId, frame, static_cast<int8_t>(playDirection));
		if (window.frames == nullptr) {
			goto getOut; // Not resident / not ready.
		}

		int32_t bytePosWithinCluster = sourceBytePos & (Cluster::size - 1);

		// Ok, how many samples can we load right now?
		int32_t bytesLeftThisSourceCluster = reversed ? (bytePosWithinCluster + bytesPerSample)
		                                              : (Cluster::size - bytePosWithinCluster + bytesPerSample - 1);
		int32_t bytesWeWantToRead = numSamplesThisClusterReadWrite * bytesPerSample;
		if (bytesWeWantToRead > bytesLeftThisSourceCluster + bytesPerSample) {
			numSamplesThisClusterReadWrite = bytesLeftThisSourceCluster / bytesPerSample;
		}

		// Do some stuff here kind of ahead of time, before we go and decrement numSamplesThisClusterReadWrite
		numSamples -= numSamplesThisClusterReadWrite;
		percCacheZone->endPos +=
		    numSamplesThisClusterReadWrite * playDirection; // Do this now, in case the next Cluster fails
		sourceBytePos += numSamplesThisClusterReadWrite * posIncrement;

		// Alright, load those samples. `window.frames` points AT this frame's own bytes; the `+ byteDepth - 4`
		// aligns for the `int32`-over-`byteDepth` read below, and the `bytesLeftThisSourceCluster` clamp keeps a
		// boundary-straddling frame reading this cluster's own physical slack.
		char* currentPos = reinterpret_cast<char*>(const_cast<void*>(window.frames)) + byteDepth - 4;

		do {
			int32_t numSamplesThisPercPixelSegment = numSamplesThisClusterReadWrite;

			int32_t numSamplesLeftThisPercPixelSegment =
			    reversed ? (startPosSamples + 1 + (kPercBufferReductionSize >> 1)) & (kPercBufferReductionSize - 1)
			             : kPercBufferReductionSize
			                   - ((startPosSamples + (kPercBufferReductionSize >> 1)) & (kPercBufferReductionSize - 1));

			if (!numSamplesLeftThisPercPixelSegment) {
				numSamplesLeftThisPercPixelSegment = kPercBufferReductionSize;
			}

			numSamplesThisPercPixelSegment =
			    std::min(numSamplesThisPercPixelSegment, numSamplesLeftThisPercPixelSegment);

			char* endPos = currentPos + numSamplesThisPercPixelSegment * posIncrement;

			int32_t angle;

			while (true) { // I've put reasonable effort into benchmarking / optimizing this loop - I don't think it can
				           // be improved much more
				int32_t thisSampleRead =
				    *(int32_t*)currentPos
				    >> 2; // Have to make it smaller even if only one, so the "angle" doesn't overflow
				if (numChannels == 2) {
					thisSampleRead += *(int32_t*)(currentPos + byteDepth) >> 2;
				}

				angle = thisSampleRead - percCacheZone->lastSampleRead;
				percCacheZone->lastSampleRead = thisSampleRead;
				if (angle < 0) {
					angle = -angle;
				}

				for (auto& pole : percCacheZone->angleLPFMem) {
					int32_t distanceToGo = angle - pole;
					pole += distanceToGo
					        >> 9; // multiply_32x32_rshift32_rounded(distanceToGo, 1 << 23); //distanceToGo >> 9;
					angle = pole;
				}

				currentPos += posIncrement;
				if (currentPos == endPos) {
					break;
				}

				percCacheZone->lastAngle = angle; // This gets skipped for the last one - and done below
			}

			startPosSamples += numSamplesThisPercPixelSegment * playDirection;

			int32_t posWithinPercPixel = startPosSamples & (kPercBufferReductionSize - 1);

			if (posWithinPercPixel == (kPercBufferReductionSize >> 1) - reversed) {

				int32_t difference = angle - percCacheZone->lastAngle;
				if (difference < 0) {
					difference = -difference;
				}

				// angle is 0 over a perfectly flat region (silence / DC / a held constant value). On the
				// ARM target an integer divide-by-zero yields 0 and playback carries on; x86 (the host
				// renderer) traps with SIGFPE instead, so guard explicitly to keep host/hardware parity.
				int32_t percussiveness = (angle != 0) ? (((uint64_t)difference * 262144 / angle) >> 1) : 0;

				percussiveness = getTanH<23>(percussiveness);

				percCacheNow[startPosSamples >> kPercBufferReductionMagnitude] = percussiveness;
			}

			percCacheZone->lastAngle = angle;

			numSamplesThisClusterReadWrite -= numSamplesThisPercPixelSegment;
		} while (numSamplesThisClusterReadWrite);

	} while (numSamples);

	percCacheZone->samplesAtStartWhichShouldBeReplaced =
	    std::max<int32_t>(2048, // 2048 is fairly arbitrary
	                      (percCacheZone->endPos - percCacheZone->startPos) * playDirection);

	// If we connected up to another, later zone...
	if (willHitNextElement) {

		// If we've extended past the samples at start which should be replaced...
		if ((endPosSamples - endPosSamplesLimit) * playDirection >= 0) {
			nextPercCacheZone->startPos = percCacheZone->startPos;
			nextPercCacheZone->samplesAtStartWhichShouldBeReplaced = percCacheZone->samplesAtStartWhichShouldBeReplaced;
			percCacheZones[reversed].erase(percCacheZones[reversed].begin() + i);
			percCacheZone = &percCacheZones[reversed][i]; // Now the merged (formerly next) zone
		}

		// Or if not...
		else {
			nextPercCacheZone->samplesAtStartWhichShouldBeReplaced -=
			    (endPosSamples - nextPercCacheZone->startPos) * playDirection;
			nextPercCacheZone->startPos = endPosSamples;
		}
	}

	// TODO: what if that next zone doesn't extend all the way to the end we want? Though that'd only very rarely
	// happen, and only hold us up very briefly

#if MEASURE_PERC_CACHE_PERFORMANCE
	{
		uint16_t endTime = MTU2.TCNT_0;
		uint16_t timeTaken = endTime - startTime;
		D_PRINTLN("perc cache fill time:  %d", timeTaken);
	}
#endif

getOut:
	// If we failed to do the loading we wanted to, e.g. because of insufficient RAM, we need to make sure we didn't
	// leave a 0-length zone, cos that's invalid.
	if (percCacheZone->endPos == percCacheZone->startPos) {
		percCacheZones[reversed].erase(percCacheZones[reversed].begin() + i);
	}

	// Unlock now that we've finished dealing with the percCacheZones array. If the below call to
	// updateClustersForPercLookahead() wants to steal any perc cache Clusters and consequently modify that array, it's
	// allowed to.
	LOCK_EXIT

	// If current source Cluster has changed, update TimeStretcher's queue
	timeStretcher->updateClustersForPercLookahead(this, sourceBytePos, playDirection);

	AudioEngine::logAction("/fillPercCache");
	return error; // Usually it'll be Error::NONE.
}

bool Sample::getAveragesForCrossfade(int32_t* totals, int32_t startBytePos, int32_t crossfadeLengthSamples,
                                     int32_t playDirection, int32_t lengthToAverageEach) {

	int32_t byteDepthNow = byteDepth;
	int32_t numChannelsNow = numChannels;
	int32_t bytesPerSample = byteDepthNow * numChannelsNow;

	const uint32_t sourceId = deluge::sample::source_id_for(*this);

	// This can happen. Not 100% sure if it should, but we'll return false just below in this case anyway, so I think
	// it's ok
	if (ALPHA_OR_BETA_VERSION && startBytePos < (int32_t)audioDataStartPosBytes) {
		FREEZE_WITH_ERROR("E283");
	}

	int32_t startSamplePos = (uint32_t)(startBytePos - audioDataStartPosBytes) / (uint8_t)bytesPerSample;

	int32_t halfCrossfadeLengthSamples = crossfadeLengthSamples >> 1;

	int32_t samplePosMidCrossfade = startSamplePos + halfCrossfadeLengthSamples * playDirection;

	int32_t readSample = samplePosMidCrossfade
	                     - ((lengthToAverageEach * TimeStretch::Crossfade::kNumMovingAverages) >> 1) * playDirection;

	int32_t halfCrossfadeLengthBytes = halfCrossfadeLengthSamples * bytesPerSample;

	int32_t readByte = readSample * bytesPerSample + audioDataStartPosBytes;

	if (playDirection == 1) {
		if (readByte < (int32_t)audioDataStartPosBytes + halfCrossfadeLengthBytes) {
			return false;
		}
		else if (readByte >= (int32_t)(audioDataStartPosBytes + audioDataLengthBytes - halfCrossfadeLengthBytes)) {
			return false;
		}
	}

	int32_t endReadByte =
	    readByte + lengthToAverageEach * TimeStretch::Crossfade::kNumMovingAverages * bytesPerSample * playDirection;

	if (endReadByte < (int32_t)(audioDataStartPosBytes - 1)
	    || endReadByte > (int32_t)(audioDataStartPosBytes + audioDataLengthBytes)) {
		return false;
	}

	for (int32_t i = 0; i < TimeStretch::Crossfade::kNumMovingAverages; i++) {

		int32_t numSamplesLeftThisAverage = lengthToAverageEach;
		totals[i] = 0;

		if (ALPHA_OR_BETA_VERSION
		    && (readByte < audioDataStartPosBytes - 1 || readByte >= audioDataStartPosBytes + audioDataLengthBytes)) {
			FREEZE_WITH_ERROR("FFFF");
		}

		do {

			if (ALPHA_OR_BETA_VERSION
			    && (readByte < audioDataStartPosBytes - 1
			        || readByte >= audioDataStartPosBytes + audioDataLengthBytes)) {
				FREEZE_WITH_ERROR("E432"); // Was "GGGG". Sven may have gotten.
			}

			int32_t whichCluster = readByte >> Cluster::size_magnitude;
			if (ALPHA_OR_BETA_VERSION
			    && (whichCluster < getFirstClusterIndexWithAudioData()
			        || whichCluster >= getFirstClusterIndexWithNoAudioData())) {
				FREEZE_WITH_ERROR("EEEE");
			}

			const uint64_t frame = (uint32_t)(readByte - audioDataStartPosBytes) / (uint8_t)bytesPerSample;
			const DelugeFrameWindow window = deluge_sample_peek(sourceId, frame, static_cast<int8_t>(playDirection));
			if (window.frames == nullptr) {
				return false; // Not resident / not ready.
			}

			int32_t bytePosWithinCluster = readByte & (Cluster::size - 1);
			int32_t numSamplesThisRead = numSamplesLeftThisAverage;

			int32_t bytesLeftThisCluster = (playDirection == -1)
			                                   ? (bytePosWithinCluster + bytesPerSample)
			                                   : (Cluster::size - bytePosWithinCluster + bytesPerSample - 1);
			int32_t bytesWeWantToRead = numSamplesThisRead * bytesPerSample;
			if (bytesWeWantToRead > bytesLeftThisCluster) {
				numSamplesThisRead = (uint32_t)bytesLeftThisCluster / (uint8_t)bytesPerSample;
			}

			// Alright, read those samples. `window.frames` points AT this frame's own bytes; the
			// `+ byteDepthNow - 4` aligns for the `int32`-over-`byteDepth` read below (its high 16
			// bits are the sample), and the `bytesLeftThisCluster` clamp keeps a boundary-straddling
			// frame reading this cluster's own physical slack.
			char* currentPos = reinterpret_cast<char*>(const_cast<void*>(window.frames)) + byteDepthNow - 4;
			char* endPos = currentPos + numSamplesThisRead * bytesPerSample * playDirection;

			do {
				totals[i] += *(int32_t*)currentPos >> 16;
				if (numChannelsNow == 2) {
					totals[i] += *(int32_t*)(currentPos + byteDepthNow) >> 16;
				}

				currentPos += bytesPerSample * playDirection;
			} while (currentPos != endPos);

			readByte += numSamplesThisRead * bytesPerSample * playDirection;
			numSamplesLeftThisAverage -= numSamplesThisRead;
			if (ALPHA_OR_BETA_VERSION && numSamplesLeftThisAverage < 0) {
				FREEZE_WITH_ERROR("DDDD");
			}
		} while (numSamplesLeftThisAverage);
	}

	return true;
}

uint8_t* Sample::prepareToReadPercCache(int32_t pixellatedPos, int32_t playDirection, int32_t* earliestPixellatedPos,
                                        int32_t* latestPixellatedPos) {

	int32_t reversed = (playDirection == 1) ? 0 : 1;

	int32_t realPos = (pixellatedPos << kPercBufferReductionMagnitude) + (kPercBufferReductionSize >> 1);
	int32_t i =
	    searchPercCacheZones(percCacheZones[reversed], realPos + 1 - reversed, reversed ? GREATER_OR_EQUAL : LESS);
	if (i < 0 || i >= static_cast<int32_t>(percCacheZones[reversed].size())) {
		return nullptr;
	}

	SamplePercCacheZone* zone = &percCacheZones[reversed][i];
	if ((zone->endPos - realPos) * playDirection <= 0) {
		return nullptr;
	}

	*earliestPixellatedPos =
	    (zone->startPos + (kPercBufferReductionSize >> 1) * playDirection) >> kPercBufferReductionMagnitude;
	*latestPixellatedPos =
	    (zone->endPos - (kPercBufferReductionSize >> 1) * playDirection) >> kPercBufferReductionMagnitude;

	// If permanently allocated perc cache...
	if (percCacheMemory[reversed]) {
		return percCacheMemory[reversed];
	}

	// Or if Cluster-based perc cache...
	else {
		int32_t ourCluster = pixellatedPos >> Cluster::size_magnitude;
		if (ALPHA_OR_BETA_VERSION && !percCacheClusters[reversed][ourCluster]) {
			FREEZE_WITH_ERROR("E142");
		}

		int32_t earliestCluster = *earliestPixellatedPos >> Cluster::size_magnitude;
		;
		int32_t latestCluster = *latestPixellatedPos >> Cluster::size_magnitude;

		// Constrain to Cluster boundaries. This will theoretically hurt the sound a tiny bit... once every 90 seconds.
		// No one will ever know
		if (earliestCluster < ourCluster) {
			*earliestPixellatedPos = ourCluster << Cluster::size_magnitude;
		}
		else if (earliestCluster > ourCluster) {
			*earliestPixellatedPos = ((ourCluster + 1) << Cluster::size_magnitude) - 1;
		}

		if (latestCluster < ourCluster) {
			*latestPixellatedPos = ourCluster << Cluster::size_magnitude;
		}
		else if (latestCluster > ourCluster) {
			*latestPixellatedPos = ((ourCluster + 1) << Cluster::size_magnitude) - 1;
		}

		// Fudge an address to send back
		return reinterpret_cast<uint8_t*>(percCacheClusters[reversed][ourCluster]->payload().data())
		       - (ourCluster * Cluster::size);
	}
}

void Sample::percCacheClusterStolen(ComputedChunk* cluster) {
	LOCK_ENTRY

	D_PRINTLN("percCacheClusterStolen -----------------------------------------------------------!!");
	int32_t reversed = (cluster->type == Cluster::Type::PERC_CACHE_REVERSED);
	int32_t playDirection = reversed ? -1 : 1;
	int32_t comparison = reversed ? GREATER_OR_EQUAL : LESS;

#if ALPHA_OR_BETA_VERSION
	if (cluster->type != Cluster::Type::PERC_CACHE_FORWARDS && cluster->type != Cluster::Type::PERC_CACHE_REVERSED) {
		FREEZE_WITH_ERROR("E149");
	}
	if (!percCacheClusters[reversed]) {
		FREEZE_WITH_ERROR("E134");
	}
	if (cluster->cluster_index >= numPercCacheClusters) {
		FREEZE_WITH_ERROR("E135");
	}
	if (!percCacheClusters[reversed][cluster->cluster_index]) {
		FREEZE_WITH_ERROR("i034"); // Trying to track down Steven G's E133 (Feb 2021).
	}
	if (deluge::cluster::lease_count(percCacheClusters[reversed][cluster->cluster_index]->resource_slot)) {
		FREEZE_WITH_ERROR("i035"); // Trying to track down Steven G's E133 (Feb 2021).
	}
#endif

	percCacheClusters[reversed][cluster->cluster_index] = nullptr;

	// TODO: while inside this, don't allow further editing to percCacheZones[reversed]

	int32_t leftBorder = cluster->cluster_index << (Cluster::size_magnitude + kPercBufferReductionMagnitude);
	int32_t rightBorder = (cluster->cluster_index + 1) << (Cluster::size_magnitude + kPercBufferReductionMagnitude);

	int32_t laterBorder = reversed ? (leftBorder - 1) : rightBorder;
	int32_t earlierBorder = reversed ? (rightBorder - 1) : leftBorder;

	// Trim anything earlier
	int32_t iEarlier;
	iEarlier = searchPercCacheZones(percCacheZones[reversed], earlierBorder + reversed, comparison);
	if (iEarlier >= 0 && iEarlier < static_cast<int32_t>(percCacheZones[reversed].size())) {
		SamplePercCacheZone* zoneEarlier = &percCacheZones[reversed][iEarlier];

		// If this zone eats into the deleted Cluster...
		if ((zoneEarlier->endPos - earlierBorder) * playDirection > 0) {

			// If it also shoots out the other side of the deleted Cluster...
			if ((zoneEarlier->endPos - laterBorder) * playDirection > 0) {
				int32_t oldStartPos = zoneEarlier->startPos;
				int32_t oldSamplesAtStartWhichShouldBeReplaced = zoneEarlier->samplesAtStartWhichShouldBeReplaced;

				zoneEarlier->startPos = laterBorder;
				zoneEarlier->samplesAtStartWhichShouldBeReplaced = 0;

				int32_t iNew = reversed ? (iEarlier + 1) : iEarlier;
				// This is reasonably likely to fail, cos it might want to allocate new memory, but that's not allowed
				// if it's currently allocating a Cluster, which it will be if this Cluster got stolen, which is why
				// we're here. Oh well
				SamplePercCacheZone newZone(oldStartPos);
				newZone.samplesAtStartWhichShouldBeReplaced = oldSamplesAtStartWhichShouldBeReplaced;
				newZone.endPos = earlierBorder;
				try {
					percCacheZones[reversed].insert(percCacheZones[reversed].begin() + iNew, newZone);
				} catch (deluge::exception&) {
					D_PRINTLN("insert fail");
				}
				LOCK_EXIT
				return;
			}

			// Or if not...
			else {
				zoneEarlier->resetEndPos(earlierBorder);
			}
		}
	}

	// Trim anything later
	int32_t iLater;
	iLater = searchPercCacheZones(percCacheZones[reversed], laterBorder + reversed, comparison);
	if ((iLater - iEarlier) * playDirection > 0) {

		SamplePercCacheZone* zoneLater = &percCacheZones[reversed][iLater];

		if ((zoneLater->endPos - laterBorder) * playDirection > 0) {
			zoneLater->samplesAtStartWhichShouldBeReplaced =
			    std::max(0_i32, zoneLater->samplesAtStartWhichShouldBeReplaced
			                        - (laterBorder - zoneLater->startPos) * playDirection);
			zoneLater->startPos = laterBorder;
		}
		else {
			goto deleteThatOneToo;
		}
	}
	else {
deleteThatOneToo:
		iLater += playDirection;
	}

	int32_t numToDelete = (iLater - iEarlier) * playDirection - 1;
	if (numToDelete) {
		int32_t deleteFrom = reversed ? (iLater + 1) : (iEarlier + 1);
		percCacheZones[reversed].erase(percCacheZones[reversed].begin() + deleteFrom,
		                               percCacheZones[reversed].begin() + deleteFrom + numToDelete);
	}

	LOCK_EXIT
}

int32_t Sample::getFirstClusterIndexWithAudioData() {
	return audioDataStartPosBytes >> Cluster::size_magnitude;
}

int32_t Sample::getFirstClusterIndexWithNoAudioData() {
	// Pure geometric count, unclamped: for a loaded sample finalizeAfterLoad() guarantees
	// audioDataStartPosBytes + audioDataLengthBytes <= fileSize, so this is <= the overview cache's
	// physical size. It is also reached for a still-recording sample (via advanceOverviewScan's
	// background scan), where audioDataLengthBytes is the kUnknownLengthSentinel; the sentinel's
	// bit-pattern deliberately makes this expression truncate (in the uint32 geometricClusterCount())
	// to 1 for every realistic cluster-size magnitude, so the scan stays bounded to cluster 0. Keep
	// that property in mind before changing the sentinel value.
	return static_cast<int32_t>(geometricClusterCount());
}

bool Sample::midiNoteWithoutScan(bool doingSingleCycle) {
	if (midiNote != MIDI_NOTE_UNSET && midiNote != MIDI_NOTE_ERROR) {
		return true;
	}
	// If doing single-cycle, easy!
	if (doingSingleCycle) {
		const float freq = (float)sampleRate / lengthInSamples;
		midiNote = 69 + log2f(freq / 440) * 12;
		return true;
	}
	// Next up, see if note read from file...
	if (midiNoteFromFile != -1) {
		midiNote = midiNoteFromFile;
		return true;
	}
	return false;
}

void Sample::setMidiNoteFromFrequency(float hz) {
	midiNote = (hz == 0) ? MIDI_NOTE_ERROR : 69 + log2f(hz / 440) * 12;
	D_PRINTLN("midiNote:  %d", midiNote);
}

uint32_t Sample::getLengthInMSec() {
	return (uint64_t)(lengthInSamples - 1) * 1000 / sampleRate + 1;
}

float getPeakIndexFloat(int32_t i, int32_t peakValue, int32_t prevValue, int32_t nextValue) {
	float fundamentalPeakIndex = i;

	int32_t nudgeInDirection = (nextValue > prevValue) ? 1 : -1;

	int32_t lowerValue = std::min(prevValue, nextValue);
	int32_t higherValue = std::max(prevValue, nextValue);

	int32_t totalDistance = peakValue - lowerValue; // Distance from lower neighbouring height to peak height

	int32_t howFarUpHigherValueIs = higherValue - lowerValue;

	float howFarAsFraction = (float)howFarUpHigherValueIs / totalDistance;

	fundamentalPeakIndex += howFarAsFraction * 0.5 * nudgeInDirection;

	return fundamentalPeakIndex;
}

const uint8_t primeNumbers[] = {2, 3, 5, 7, 11, 13};
constexpr int32_t kNumPrimes = 6;

// Returns strength
int32_t Sample::investigateFundamentalPitch(int32_t fundamentalIndexProvided, int32_t tableSize, int32_t* heightTable,
                                            uint64_t* sumTable, float* floatIndexTable, float* getFundamentalIndex,
                                            int32_t numDoublings, bool doPrimeTest) {

	uint64_t total = 0;

	uint64_t primeTotals[kNumPrimes];
	if (true || doPrimeTest) {
		memset(primeTotals, 0, sizeof(primeTotals));
	}

	float uncertaintyCount = 1.5;
	float fundamentalIndexToReturn;
	float fundamentalIndexForContinuedHarmonicInvestigation;
	float uncertaintyMarginHere;

	int32_t currentIndex = fundamentalIndexProvided;
	int32_t h = 1; // The number of the harmonic currently being investigated
	int32_t lastHFound = 1;

	uint64_t lastSumTableValue = sumTable[fundamentalIndexProvided >> 1];

	goto examineHarmonic;

	while (true) {

		{
			if (uncertaintyCount >= 10.5) {
				break; // Probably not really necessary
			}

			if (h == 16) {
				break; // Limit number of harmonics investigated
			}
			h++;

			uncertaintyMarginHere = uncertaintyCount;

			if (uncertaintyMarginHere < 2) {
				uncertaintyMarginHere = 2;
			}

			if (uncertaintyMarginHere > (fundamentalIndexProvided >> 1)) {
				uncertaintyMarginHere = (fundamentalIndexProvided >> 1);
			}

			float searchCentre =
			    fundamentalIndexForContinuedHarmonicInvestigation * h + 0.5; // Will round when converted to int32_t

			int32_t searchMax = searchCentre + uncertaintyMarginHere;
			if (searchMax >= tableSize) {
				break;
			}
			int32_t searchMin = searchCentre - uncertaintyMarginHere;

			int32_t highestFoundHere = 0;

			for (int32_t proposedIndex = searchMin; proposedIndex <= searchMax; proposedIndex++) {
				int32_t valueHere = heightTable[proposedIndex];
				if (valueHere > highestFoundHere) {
					highestFoundHere = valueHere;
					currentIndex = proposedIndex;
				}
			}

			uncertaintyCount += (float)1.5 / lastHFound;

			if (!highestFoundHere) {
				continue;
			}
		}

examineHarmonic:
		float newEstimatedFundamentalIndex = floatIndexTable[currentIndex >> 1] / h;

		int32_t nextMidIndex = currentIndex + ((fundamentalIndexProvided + 1) >> 1); // Round up
		uint64_t nextSumTableValue = sumTable[nextMidIndex];
		uint64_t surroundingSum = nextSumTableValue - lastSumTableValue;

		lastSumTableValue = nextSumTableValue;

		int32_t heightRightHere = heightTable[currentIndex];
		int32_t heightRelativeToSurroundings = ((uint64_t)heightRightHere << 18) / surroundingSum;

		int32_t strengthThisHarmonic = ((uint64_t)heightRelativeToSurroundings * (uint64_t)heightRightHere) >> 20;
		total += strengthThisHarmonic;

		if (h == 1) {
			fundamentalIndexForContinuedHarmonicInvestigation = newEstimatedFundamentalIndex;
		}

		else {
			float distanceToGo = newEstimatedFundamentalIndex - fundamentalIndexForContinuedHarmonicInvestigation;
			float heightRelativeToSurroundingsFloat = (float)heightRelativeToSurroundings / (1 << 18);
			if (heightRelativeToSurroundingsFloat > 1) {
				heightRelativeToSurroundingsFloat = 1;
			}
			fundamentalIndexForContinuedHarmonicInvestigation += distanceToGo * heightRelativeToSurroundingsFloat;

			float uncertaintyReduction = heightRelativeToSurroundingsFloat * 8;
			if (uncertaintyReduction < 1) {
				uncertaintyReduction = 1;
			}

			uncertaintyCount /= uncertaintyReduction;
			if (uncertaintyCount < 1.5) {
				uncertaintyCount = 1.5;
			}
		}

		if (true || doPrimeTest) {
			for (int32_t p = 0; p < kNumPrimes; p++) {
				if (p == 0 && !doPrimeTest) {
					continue;
				}

				uint8_t thisPrime = primeNumbers[p];
				if (thisPrime > h) {
					break;
				}

				if (!((uint32_t)h % thisPrime)) {
					primeTotals[p] += strengthThisHarmonic;
				}
			}
		}

		// After working far enough into the table, we want to stop adjusting the pitch we're going to output, because
		// the higher harmonics tend to be a bit sharp, at least initially, on a lot of acoustic instruments.
		if (h == 1 || currentIndex < 128) {
			fundamentalIndexToReturn = fundamentalIndexForContinuedHarmonicInvestigation;
		}

		lastHFound = h;

#if PITCH_DETECT_DEBUG_LEVEL >= 2
		D_PRINTLN("found harmonic  %d . value  %d ,  %d", h, heightTable[currentIndex],
		          (heightRelativeToSurroundings * 100) >> 18);
		float fundamentalPeriod = (float)PITCH_DETECT_WINDOW_SIZE / fundamentalIndexForContinuedHarmonicInvestigation;
		float freqBeforeAdjustment = (float)sampleRate / fundamentalPeriod;
		float freq = freqBeforeAdjustment / (1 << numDoublings);
		D_PRINT("%. proposed freq: ");
		Debug::printfloat(freq);
		D_PRINTLN(". uc:  %d", uncertaintyCount);
		delayMS(30);
#endif
	}

	*getFundamentalIndex = fundamentalIndexToReturn;

	int32_t threshold = 6;

	if (true || doPrimeTest) {
		for (int32_t p = 0; p < kNumPrimes; p++) {
			uint8_t thisPrime = primeNumbers[p];
			if (thisPrime > h) {
				break;
			}

			// if (primeTotals[p] >= (total - primeTotals[p]) / (thisPrime - 1) * threshold) return 0;
			if (primeTotals[p] * (thisPrime - 1) >= (total - primeTotals[p]) * threshold) {
				// D_PRINTLN("failing due to prime thing");
				return 0;
			}
		}
	}

	// Too low and piano doesn't work. Too high and vibraphone doesn't work

	// With FFT m=12
	// No delay: doesn't work
	// 1/8 second delay: 0.35 to 0.40
	// 1/4 second delay: 0.25 to 0.55
	// 1/2 second delay: 0.65 is max for vibraphone. Never quite get all piano working

	// With FFT m=13
	// 1/4 second delay: -0.05 to 0.55

	return (uint64_t)(total * powf(fundamentalIndexToReturn, 0.25));
}

// We want a fairly small window. Any bigger, and it'll fail to find the tones in short, percussive yet tonal sounds.
// Or if we were to go much smaller than this, we might incorrectly see low frequencies.
// Already, this is too small to very accurately pick up low frequencies, so when one is detected, a second pass is done
// on downsampled (squished in) audio data, to pick it up more accurately

size_t Sample::pitchBufferBytes() {
	const int32_t fftInputSize = kPitchDetectWindowSize * sizeof(int32_t);
	const int32_t fftOutputSize = ((kPitchDetectWindowSize >> 1) + 1) * sizeof(ne10_fft_cpx_int32_t);
	const int32_t floatIndexTableSize = (kPitchDetectWindowSize >> 2) * sizeof(float);
	return fftInputSize + fftOutputSize + floatIndexTableSize;
}

Sample::PitchVerdict Sample::analysePitchWindow(int32_t* buffer, int32_t lengthDoublings,
                                                int32_t defaultLengthDoublings, float minFreqHz, float maxFreqHz,
                                                bool doPrimeTest) {
	// Get the FFT config we'll need
	ne10_fft_r2c_cfg_int32_t fftCFG = FFTConfigManager::getConfig(kPitchDetectWindowSizeMagnitude);
	int32_t* fftInput = buffer;
	const int32_t fftInputSize = kPitchDetectWindowSize * sizeof(int32_t);
	const int32_t fftOutputSize = ((kPitchDetectWindowSize >> 1) + 1) * sizeof(ne10_fft_cpx_int32_t);
	ne10_fft_cpx_int32_t* fftOutput = (ne10_fft_cpx_int32_t*)((char*)fftInput + fftInputSize);
	int32_t* fftHeights = fftInput; // We'll overwrite the original input with this data
	float* floatIndexTable = (float*)((char*)fftInput + fftInputSize + fftOutputSize);

	AudioEngine::routineWithClusterLoading();

	/*
	D_PRINTLN("doing fft ---------------- %d", PITCH_DETECT_WINDOW_SIZE_MAGNITUDE);
	uint16_t startTime = MTU2.TCNT_0;
	*/

	// Perform the FFT
	ne10_fft_r2c_1d_int32_neon(fftOutput, (ne10_int32_t*)fftInput, fftCFG, false);

	// D_PRINTLN("fft done");

	/*
	uint16_t endTime = MTU2.TCNT_0;
	uint16_t time = endTime - startTime;
	D_PRINTLN("fft time uSec:  %d", timerCountToUS(time));
	*/
	AudioEngine::logAction("bypassing culling in pitch detection");
	AudioEngine::bypassCulling = true;
	AudioEngine::routineWithClusterLoading();

	// Go through complex-number FFT result, converting to positive (pythagorassed) heights
	int32_t biggestValue = 0;
	for (int32_t i = 0; i < (kPitchDetectWindowSize >> 1); i++) {

		if (!(i & 1023)) {
			AudioEngine::routineWithClusterLoading();
		}

		int32_t thisValue = fastPythag(fftOutput[i].r, fftOutput[i].i);
		if (thisValue > biggestValue) {
			biggestValue = thisValue;
		}

		fftHeights[i] = thisValue;

#if PITCH_DETECT_DEBUG_LEVEL >= 2
		if (!(i & 31)) {
			D_PRINTLN("");
			D_PRINT(i);
			D_PRINT(": ");
			delayMS(50);
		}
		D_PRINT(thisValue);
		D_PRINT(", ");
#endif
	}

	int32_t minFreqForThresholdAdjusted = 200 << lengthDoublings;
	float minPeriodForThreshold = sampleRate / minFreqForThresholdAdjusted;
	int32_t minIndexForThreshold = (float)kPitchDetectWindowSize / minPeriodForThreshold; // Rounds down

	uint64_t sum = 0;
	int32_t lastValue1 = 0;
	int32_t lastValue2 = 0;
	int32_t threshold = biggestValue >> 10;

	// Go through again doing the running sum, interpolating exact peak frequencies, and deleting everything that's not
	// a peak
	for (int32_t i = 0; i < (kPitchDetectWindowSize >> 1); i++) {

		if (!(i & 255)) {
			AudioEngine::routineWithClusterLoading();
		}

		int32_t thisValue = fftHeights[i];

		// Don't bother with anything under the threshold - mostly just for efficiency, since the threshold is very low
		// and won't cause much real-world difference. Don't do it below a certain freq though - we absolutely need even
		// the tiniest peaks down in the 30hz kind of range (see Leo's pianos)
		bool shouldWriteZeroBack = (i >= minIndexForThreshold && lastValue1 < threshold);
		if (!shouldWriteZeroBack) {
			bool isPeakHere = (i >= 2 && thisValue < lastValue1 && lastValue1 >= lastValue2);

			if (isPeakHere) {
				floatIndexTable[(i - 1) >> 1] = getPeakIndexFloat(i - 1, lastValue1, lastValue2, thisValue);
			}

			shouldWriteZeroBack = !isPeakHere;
		}

		if (i >= 1 && shouldWriteZeroBack) {
			fftHeights[i - 1] = 0;
		}

		sum += lastValue1;
		*(uint64_t*)&fftOutput[i] = sum;

		lastValue2 = lastValue1;
		lastValue1 = thisValue;
	}

#if PITCH_DETECT_DEBUG_LEVEL
	D_PRINTLN("");
#endif

	int32_t minFreqAdjusted = minFreqHz * (1 << lengthDoublings);
	float minFundamentalPeriod = (float)sampleRate / minFreqAdjusted;
	int32_t minFundamentalPeakIndex = (float)kPitchDetectWindowSize / minFundamentalPeriod; // Rounds down

	int32_t maxFreqAdjusted = maxFreqHz * (1 << lengthDoublings);
	float maxFundamentalPeriod = (float)sampleRate / maxFreqAdjusted;
	int32_t maxFundamentalPeakIndex = (float)kPitchDetectWindowSize / maxFundamentalPeriod + 1; // Rounds up
	if (maxFundamentalPeakIndex > (kPitchDetectWindowSize >> 1)) {
		maxFundamentalPeakIndex = (kPitchDetectWindowSize >> 1);
	}

	float bestFundamentalIndex;
	int32_t bestStrength = 0;

	int32_t peakCount = 0;

	// For each peak, evaluate its strength as a contender for the fundamental
	for (int32_t i = minFundamentalPeakIndex; i < maxFundamentalPeakIndex; i++) {

		if (!fftHeights[i]) {
			continue;
		}

		// We're at a peak!

		if (!(peakCount & 7)) {
			AudioEngine::routineWithClusterLoading(); // Rohan 15 works. 7 is extra safe
		}
		peakCount++;

		float fundamentalIndexHere;
		int32_t strengthHere =
		    investigateFundamentalPitch(i, (kPitchDetectWindowSize >> 1), fftHeights, (uint64_t*)fftOutput,
		                                floatIndexTable, &fundamentalIndexHere, lengthDoublings, doPrimeTest);

#if PITCH_DETECT_DEBUG_LEVEL
		if (PITCH_DETECT_DEBUG_LEVEL >= 2 || strengthHere > bestStrength) {
			delayMS(10);

			float fundamentalPeriod = (float)PITCH_DETECT_WINDOW_SIZE / fundamentalIndexHere;
			float freqBeforeAdjustment = (float)sampleRate / fundamentalPeriod;
			float freq = freqBeforeAdjustment / (1 << lengthDoublings);

			D_PRINTLN("strength  %d  at i  %d , freq  %d", strengthHere, i, freq);
#if PITCH_DETECT_DEBUG_LEVEL >= 2
			D_PRINTLN("");
#endif
		}
#endif

		if (strengthHere > bestStrength) {

			bestStrength = strengthHere;
			bestFundamentalIndex = fundamentalIndexHere;
		}
	}

	// If no peaks found, print out the FFT for debugging
	if (!bestStrength) {
		D_PRINTLN("no peaks found.");

		D_PRINTLN("searching  %d  to  %d", minFundamentalPeakIndex, maxFundamentalPeakIndex);

#if PITCH_DETECT_DEBUG_LEVEL
		for (int32_t i = 0; i < (PITCH_DETECT_WINDOW_SIZE >> 1); i++) {
			if (!(i & 31)) {
				D_PRINTLN("");
				D_PRINT(i);
				D_PRINT(": ");
				delayMS(50);
			}
			D_PRINT(fftHeights[i]);
			D_PRINT(", ");
		}
#endif
		return {PitchVerdict::Kind::Failed, 0};
	}

	float fundamentalPeriod = (float)kPitchDetectWindowSize / bestFundamentalIndex;

	float freqBeforeAdjustment = (float)sampleRate / fundamentalPeriod;

	int32_t lengthDoublingsLastTime = lengthDoublings;

	// If frequency too low, go again, taking a longer length into account, for better accuracy
	if (freqBeforeAdjustment < deluge::sample::kMinAccurateFrequency
	    && lengthDoublings < defaultLengthDoublings + deluge::sample::kMaxLengthDoublings) {

#if PITCH_DETECT_DEBUG_LEVEL
		float freq = freqBeforeAdjustment / (1 << lengthDoublings);
		D_PRINTLN("proposed freq: ");
#endif
		// Only do one doubling at a time - this can help to correct an incorrect reading
		return {PitchVerdict::Kind::LongerLength, 0};
	}

	float freq = freqBeforeAdjustment / (1 << lengthDoublings);
	D_PRINTLN("freq: %.2f", freq);
	return {PitchVerdict::Kind::Found, freq};
}

int32_t Sample::getMaxPeakFromZero() {
	// Comes out one >> of the value we actually want
	int32_t halfValue = std::abs(getFoundValueCentrePoint() >> 1) + (maxValueFound >> 2) - (minValueFound >> 2);

	// Does the <<1 and saturates it - this was necessary, it was overflowing sometimes - I think when the audio clipped
	return lshiftAndSaturate<1>(halfValue);
}

int32_t Sample::getFoundValueCentrePoint() {
	return (maxValueFound >> 1) + (minValueFound >> 1);
}

// Returns the value span divided by display height
int32_t Sample::getValueSpan() {
	return (maxValueFound >> kDisplayHeightMagnitude) - (minValueFound >> kDisplayHeightMagnitude);
}

void Sample::finalizeAfterLoad(uint32_t fileSize) {

	audioDataLengthBytes = std::min<uint64_t>(audioDataLengthBytes, fileSize - audioDataStartPosBytes);

	uint32_t bytesPerSample = byteDepth * numChannels;

	audioDataLengthBytes = std::min<uint64_t>(audioDataLengthBytes, fileSize - audioDataStartPosBytes);

	lengthInSamples = audioDataLengthBytes / bytesPerSample;
	audioDataLengthBytes = lengthInSamples * bytesPerSample; // Make sure it's an exact number of samples

	workOutBitMask();
}

// Soft-reference (on) / un-reference (off) every resource asset this sample owns: its cluster asset
// plus each derived cache (the two perc-cache directions + the repitch caches). The manager's value
// function keeps soft-referenced (current-song) chunks resident over un-referenced (no-song) ones.
// unreference saturates at 0, so the toggle is robust even if an asset was created/retired mid-window.
void Sample::applyProjectReference(bool on) {
	DelugeResource* mgr = GeneralMemoryAllocator::get().resourceManager();
	if (mgr == nullptr) {
		return;
	}
	auto toggle = [&](uint32_t asset) {
		if (asset == DELUGE_RESOURCE_NO_ASSET) {
			return;
		}
		if (on) {
			deluge_resource_reference(mgr, asset);
		}
		else {
			deluge_resource_unreference(mgr, asset);
		}
	};
	toggle(stream_.resource_asset_id());
	toggle(percCacheAssetId[0]);
	toggle(percCacheAssetId[1]);
	for (SampleCacheElement& element : caches) {
		if (element.cache != nullptr) {
			toggle(element.cache->resourceAssetId);
		}
	}
}

void Sample::numReasonsIncreasedFromZero() {
	// Became project-relevant (first hard lease on the object) → soft-reference our resource assets.
	applyProjectReference(true);
}

void Sample::numReasonsDecreasedToZero([[maybe_unused]] char const* errorCode) {
	// No longer project-relevant → drop the soft-references (assets stay resident but now evict before
	// current-song data under pressure).
	applyProjectReference(false);
}
