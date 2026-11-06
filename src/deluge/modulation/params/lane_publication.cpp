/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "modulation/params/lane_publication.h"

#include "deluge-project/edit_guard.h"
#include "libdeluge/system.h"
#include "libdeluge/tier.h"
#include "memory/heaps.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/params/lane_snapshot.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_manager.h"
#include "playback/move_section.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "util/spsc_ring.h"

#include <algorithm>
#include <atomic>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t renderModelReads = 0;
uint32_t staleLanes = 0;
uint32_t publishFailures = 0;
uint32_t renderPublishedWrites = 0;
uint32_t laneRequestOverflows = 0;
#endif

namespace {
/// Whether a collection was marked or made since the last walk of the song (written by either tier).
std::atomic<bool> g_anyPending{false};
/// Whether the render has read lanes live since the last walk that published every collection the song plays: after
/// record mode the render keeps reading them live until that walk has published what it wrote.
std::atomic<bool> g_liveUntilSettled{false};
/// [interaction] Operations in progress: none may publish until the outermost ends.
int32_t g_operations = 0;

/// Whether the interaction tier may publish now: not on the render, with no section holding the render off and no
/// operation half done.
bool mayPublish() {
	return !deluge::memory::onRenderTier() && deluge_tier_current() == DELUGE_TIER_INTERACTION
	       && deluge_audio_excluded_depth() == 0 && g_operations == 0;
}

/// Call @p visit with every collection of every param manager @p song plays -- its own, each clip's and each note
/// row's -- and that manager. A collection parked in undo history or awaiting retirement is not among them: its stores
/// may already be gone.
void forEachPlayedCollection(Song& song, deluge::function_ref<void(ParamCollection&, ParamManagerForTimeline&)> visit) {
	auto visitManager = [&](ParamManagerForTimeline& manager) {
		for (ParamCollectionSummary& summary : manager.summaries) {
			if (summary.paramCollection == nullptr) {
				break;
			}
			visit(*summary.paramCollection, manager);
		}
	};
	visitManager(song.paramManager);
	for (Clip* clip : AllClips::everywhere(&song)) {
		visitManager(clip->paramManager);
		if (clip->type == ClipType::INSTRUMENT) {
			for (NoteRow& row : static_cast<InstrumentClip*>(clip)->noteRows) {
				visitManager(row.paramManager);
			}
		}
	}
}
} // namespace

namespace {
/// What the render asks the interaction tier to do to a lane.
struct LaneRequest {
	enum class Kind : uint8_t { BASE, STRAY_NODE } kind = Kind::BASE;
	/// The collection, compared by address with those the song plays, never dereferenced unless it is one of them.
	ParamCollection* collection = nullptr;
	uint32_t serial = 0;
	int32_t paramId = 0;
	int32_t value = 0; ///< BASE: the base; STRAY_NODE: the length a last node at or past is erased
};

/// [render] -> [interaction] The render's lane requests. A dropped one is counted: a dropped stray node is asked for
/// again the next time the render reaches it; a dropped base is left as it was.
deluge::util::SpscRing<LaneRequest, 64> g_laneRequests;

void postLaneRequest(const LaneRequest& request) {
	if (!g_laneRequests.try_push(request)) {
#if ALPHA_OR_BETA_VERSION
		laneRequestOverflows++;
#endif
	}
}

/// The collection @p request names, if @p song still plays it.
ParamCollection* stillPlayed(Song& song, const LaneRequest& request) {
	ParamCollection* found = nullptr;
	forEachPlayedCollection(song, [&](ParamCollection& collection, ParamManagerForTimeline&) {
		if (&collection == request.collection && collection.serial() == request.serial) {
			found = &collection;
		}
	});
	return found;
}
} // namespace

void requestLaneBase(ParamCollection& collection, int32_t paramId, int32_t value) {
	postLaneRequest({.kind = LaneRequest::Kind::BASE,
	                 .collection = &collection,
	                 .serial = collection.serial(),
	                 .paramId = paramId,
	                 .value = value});
}

void requestStrayNodeErased(ParamCollection& collection, int32_t paramId, int32_t length) {
	postLaneRequest({.kind = LaneRequest::Kind::STRAY_NODE,
	                 .collection = &collection,
	                 .serial = collection.serial(),
	                 .paramId = paramId,
	                 .value = length});
}

void applyLaneRequests() {
	LaneRequest request;
	while (g_laneRequests.try_pop(request)) {
		ParamCollection* collection = (currentSong != nullptr) ? stillPlayed(*currentSong, request) : nullptr;
		if (collection == nullptr) {
			continue;
		}
		project::EditGuard guard{project::ModelEdit::NODE_KERNEL};
		if (request.kind == LaneRequest::Kind::BASE) {
			if (project::DocAutoParam* lane = collection->docParamForId(request.paramId)) {
				project::set_base(*lane, request.value);
			}
		}
		else if (project::DocAutoParam* lane = collection->findDocParamForId(request.paramId);
		         lane != nullptr && !lane->nodes.empty()) {
			const int32_t last = std::ssize(lane->nodes) - 1;
			if (lane->nodes[last].pos >= request.value) {
				lane->nodes.erase(lane->nodes.begin() + last);
			}
		}
		markLanesChanged(*collection);
	}
}

bool lanesLive() {
	if (playbackHandler.recording != RecordingMode::OFF) {
		g_liveUntilSettled.store(true, std::memory_order_relaxed);
		return true;
	}
	return g_liveUntilSettled.load(std::memory_order_relaxed);
}

void noteRenderModelRead() {
#if ALPHA_OR_BETA_VERSION
	if (!lanesLive()) {
		renderModelReads++;
	}
#endif
}

bool playsPublished() {
	return (deluge::memory::onRenderTier() || deluge::playback::MoveEffects::collecting()) && !lanesLive();
}

const LaneSnapshot* snapshotToPlay(const ParamCollection& collection) {
	if (!playsPublished()) {
		return nullptr;
	}
	const LaneSnapshot* published = collection.published();
	if (published == nullptr) {
		noteRenderModelRead();
	}
	return published;
}

void noteRenderLaneWrite() {
#if ALPHA_OR_BETA_VERSION
	if (deluge::memory::onRenderTier() && !lanesLive()) {
		renderPublishedWrites++;
	}
#endif
}

void LanePublication::made(ParamCollection& /*collection*/) {
	g_anyPending.store(true, std::memory_order_relaxed);
}

void LanePublication::gone(ParamCollection& collection) {
	// Freed now, not retired: the render reaches a snapshot only through its collection, within a block, and a
	// collection being destroyed is already out of the render's reach (or the collection itself would be read freed).
	// Retiring would also hold back a load's wait for the outgoing song, whose collections go when it is freed.
	if (const LaneSnapshot* old = collection.published_.exchange(nullptr)) {
		for (const LaneSnapshot::Entry& entry : old->entries()) {
			PublishedLane::destroy(const_cast<PublishedLane*>(entry.lane));
		}
		LaneSnapshot::destroy(const_cast<LaneSnapshot*>(old));
	}
}

void LanePublication::mark(ParamCollection& collection) {
	collection.lanesDirty_ = true;
	g_anyPending.store(true, std::memory_order_relaxed);
}

bool LanePublication::matches(ParamCollection& collection) {
	const LaneSnapshot* published = collection.published();
	uint32_t lanes = 0;
	bool same = true;
	collection.forEachLane([&](int32_t key, const project::DocAutoParam& lane) {
		lanes++;
		const PublishedLane* mine = (published != nullptr) ? published->find(key) : nullptr;
		same = same && mine != nullptr && mine->equals(lane);
	});
	return published != nullptr && same && lanes == published->count;
}

bool LanePublication::publish(ParamCollection& collection, ParamManagerForTimeline& manager) {
	if (collection.published() != nullptr && matches(collection)) {
		return true;
	}
	const LaneSnapshot* old = collection.published();

	uint32_t lanes = 0;
	collection.forEachLane([&](int32_t, const project::DocAutoParam&) { lanes++; });
	LaneSnapshot* next = LaneSnapshot::make(lanes);
	if (next == nullptr) {
		return false;
	}
	// Each lane the old snapshot holds unchanged is shared; the rest are copied
	std::span<LaneSnapshot::Entry> entries = next->entriesToFill();
	uint32_t filled = 0;
	bool outOfMemory = false;
	collection.forEachLane([&](int32_t key, const project::DocAutoParam& lane) {
		if (outOfMemory || filled == entries.size()) {
			return;
		}
		const PublishedLane* kept = (old != nullptr) ? old->find(key) : nullptr;
		const PublishedLane* published = (kept != nullptr && kept->equals(lane)) ? kept : PublishedLane::copyOf(lane);
		if (published == nullptr) {
			outOfMemory = true;
			return;
		}
		entries[filled++] = {.key = key, .lane = published};
	});
	if (outOfMemory) {
		for (uint32_t i = 0; i < filled; i++) {
			if (old == nullptr || old->find(entries[i].key) != entries[i].lane) {
				PublishedLane::destroy(const_cast<PublishedLane*>(entries[i].lane));
			}
		}
		LaneSnapshot::destroy(next);
		return false;
	}
	std::ranges::sort(entries, {}, &LaneSnapshot::Entry::key);

	{
		// The tick re-reads the collection's lanes at its next block, from the new snapshot. Not a MoveSection: its end
		// would publish again from inside this publish.
		AudioExcludedSection masked;
		deluge::memory::NoAllocationScope noAllocation;
		deluge::playback::MaskedSpan span{"publishLanes"};
		collection.published_.store(next, std::memory_order_release);
		if (old != nullptr) { // a first publish replaces the model the render read, which holds the same lanes
			collection.ticksTilNextEvent = 0;
			manager.ticksTilNextEvent = 0;
		}
	}

	if (old != nullptr) {
		for (const LaneSnapshot::Entry& entry : old->entries()) {
			if (next->find(entry.key) != entry.lane) {
				AudioEngine::retireMemory(const_cast<PublishedLane*>(entry.lane), &PublishedLane::destroy);
			}
		}
		AudioEngine::retireMemory(const_cast<LaneSnapshot*>(old), &LaneSnapshot::destroy);
	}
	return true;
}

void LanePublication::publishDirty(bool settle) {
	static bool publishing = false;
	const bool settleAll =
	    settle && g_liveUntilSettled.load(std::memory_order_relaxed) && playbackHandler.recording == RecordingMode::OFF;
	if (currentSong == nullptr || publishing || !mayPublish()
	    || (!g_anyPending.load(std::memory_order_relaxed) && !settleAll)) {
		return;
	}
	publishing = true;
	g_anyPending.store(false, std::memory_order_relaxed);
	bool failed = false;
	// A collection stays marked until the job settles: a caller may still write a lane it holds after this publish,
	// and the next publish point takes that write.
	forEachPlayedCollection(*currentSong, [&](ParamCollection& collection, ParamManagerForTimeline& manager) {
		if (!collection.lanesDirty_ && collection.published() != nullptr && !settleAll) {
			return;
		}
		if (!publish(collection, manager)) {
			failed = true;
#if ALPHA_OR_BETA_VERSION
			publishFailures++;
#endif
			return;
		}
#if ALPHA_OR_BETA_VERSION
		if (!matches(collection)) {
			staleLanes++;
		}
#endif
		if (settle) {
			collection.lanesDirty_ = false;
		}
	});
	if (failed || !settle) {
		g_anyPending.store(true, std::memory_order_relaxed); // the next publish point looks again
	}
	if (settleAll && !failed) {
		g_liveUntilSettled.store(false, std::memory_order_relaxed);
	}
	publishing = false;
}

bool LanePublication::publishSong(Song& song) {
	if (!mayPublish()) {
		return false;
	}
	bool published = true;
	forEachPlayedCollection(song, [&](ParamCollection& collection, ParamManagerForTimeline& manager) {
		published = publish(collection, manager) && published;
	});
	return published;
}

int32_t LanePublication::check(Song& song) {
	int32_t differing = 0;
	forEachPlayedCollection(song, [&](ParamCollection& collection, ParamManagerForTimeline&) {
		if (collection.published() != nullptr && !matches(collection)) {
			differing++;
		}
	});
	return differing;
}

int32_t checkPublished(Song& song) {
	return LanePublication::check(song);
}

LaneOperation::LaneOperation() {
	if (!deluge::memory::onRenderTier()) {
		g_operations++;
	}
}

LaneOperation::~LaneOperation() {
	if (deluge::memory::onRenderTier()) {
		return;
	}
	if (--g_operations == 0) {
		try {
			LanePublication::publishDirty(false);
		} catch (...) {
			// Out of memory building a snapshot: the lanes stay marked for the next publish point
		}
	}
}

void markLanesChanged(ParamCollection& collection) {
	if (deluge::memory::onRenderTier()) {
		noteRenderModelRead(); // the render reached a lane in the model
	}
	else {
		LanePublication::mark(collection);
	}
}

void publishDirtyLanes(bool settle) {
	LanePublication::publishDirty(settle);
}

bool publishSongLanes(Song& song) {
	return LanePublication::publishSong(song);
}

} // namespace deluge::modulation
