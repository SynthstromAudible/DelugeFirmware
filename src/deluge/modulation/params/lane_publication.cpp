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
#include <array>
#include <atomic>
#include <span>

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
/// [interaction] Whether a walk is publishing: a publish point it reaches (an operation ending inside it) waits.
bool g_publishing = false;

/// Whether the interaction tier may publish now: not on the render, with no section holding the render off and no
/// operation half done.
bool mayPublish() {
	return !deluge::memory::onRenderTier() && deluge_tier_current() == DELUGE_TIER_INTERACTION
	       && deluge_audio_excluded_depth() == 0 && g_operations == 0;
}

/// Free a snapshot never published, @p next, and the lanes it was built with (@p built) that @p old does not share.
void discard(std::span<const LaneSnapshot::Entry> built, LaneSnapshot* next, const LaneSnapshot* old) {
	for (const LaneSnapshot::Entry& entry : built) {
		if (old == nullptr || old->find(entry.key) != entry.lane) {
			PublishedLane::destroy(const_cast<PublishedLane*>(entry.lane));
		}
	}
	LaneSnapshot::destroy(next);
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
/// What the render asks the interaction tier to do to a lane.
struct LaneRequest {
	enum class Kind : uint8_t { BASE, STRAY_NODE } kind = Kind::BASE;
	/// The collection, compared by address with those the song plays, never dereferenced unless it is one of them.
	ParamCollection* collection = nullptr;
	uint32_t serial = 0;
	int32_t paramId = 0;
	int32_t value = 0; ///< BASE: the new base; STRAY_NODE: the position of the node to erase
	int32_t seen = 0;  ///< BASE: the base the render saw, which the new one replaces only if still there
};

/// [render] -> [interaction] The render's lane requests. A dropped one is counted: a dropped stray node is asked for
/// again the next time the render reaches it; a dropped base is left as it was. The render posts a base only when it
/// changes, so a run of stops posts only the expression it must clear.
constexpr size_t kLaneRequests = 64;
deluge::util::SpscRing<LaneRequest, kLaneRequests> g_laneRequests;

void postLaneRequest(const LaneRequest& request) {
	if (!g_laneRequests.try_push(request)) {
#if ALPHA_OR_BETA_VERSION
		laneRequestOverflows++;
#endif
	}
}

/// For each of @p requests, the collection it names if @p song still plays it, else nullptr: one walk of the song.
void resolve(Song& song, std::span<const LaneRequest> requests, std::span<ParamCollection*> collections) {
	std::ranges::fill(collections, nullptr);
	forEachPlayedCollection(song, [&](ParamCollection& collection, ParamManagerForTimeline&) {
		for (size_t i = 0; i < requests.size(); i++) {
			if (&collection == requests[i].collection && collection.serial() == requests[i].serial) {
				collections[i] = &collection;
			}
		}
	});
}
} // namespace

void requestLaneBase(ParamCollection& collection, int32_t paramId, int32_t seen, int32_t value) {
	if (seen == value) {
		return;
	}
	postLaneRequest({.kind = LaneRequest::Kind::BASE,
	                 .collection = &collection,
	                 .serial = collection.serial(),
	                 .paramId = paramId,
	                 .value = value,
	                 .seen = seen});
}

void requestStrayNodeErased(ParamCollection& collection, int32_t paramId, int32_t pos) {
	postLaneRequest({.kind = LaneRequest::Kind::STRAY_NODE,
	                 .collection = &collection,
	                 .serial = collection.serial(),
	                 .paramId = paramId,
	                 .value = pos});
}

void applyLaneRequests() {
	std::array<LaneRequest, kLaneRequests> requests;
	size_t count = 0;
	while (count < requests.size() && g_laneRequests.try_pop(requests[count])) {
		count++;
	}
	if (count == 0 || currentSong == nullptr) {
		return;
	}
	std::array<ParamCollection*, kLaneRequests> collections;
	resolve(*currentSong, std::span(requests).first(count), std::span(collections).first(count));
	for (size_t i = 0; i < count; i++) {
		const LaneRequest& request = requests[i];
		ParamCollection* collection = collections[i];
		if (collection == nullptr) {
			continue;
		}
		project::EditGuard guard{project::ModelEdit::NODE_KERNEL};
		if (request.kind == LaneRequest::Kind::BASE) {
			if (project::DocAutoParam* lane = collection->docParamForId(request.paramId);
			    lane != nullptr && lane->current_value == request.seen) {
				project::set_base(*lane, request.value);
			}
		}
		else if (project::DocAutoParam* lane = collection->findDocParamForId(request.paramId);
		         lane != nullptr && !lane->nodes.empty()) {
			const int32_t last = std::ssize(lane->nodes) - 1;
			if (lane->nodes[last].pos == request.value) {
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
	if (deluge::memory::onRenderTier() && !lanesLive()) {
		renderModelReads++;
	}
#endif
}

bool playsPublished() {
	return deluge::memory::onRenderTier() && !lanesLive();
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
		discard(entries.first(filled), next, old);
		return false;
	}
	std::ranges::sort(entries, {}, &LaneSnapshot::Entry::key);

	// Room to retire what the new snapshot replaces, made before the swap so retiring it cannot fail
	size_t retirements = 0;
	if (old != nullptr) {
		retirements = 1 + std::ranges::count_if(old->entries(), [&](const LaneSnapshot::Entry& entry) {
			              return next->find(entry.key) != entry.lane;
		              });
	}
	try {
		AudioEngine::reserveRetirements(retirements);
	} catch (...) {
		discard(entries, next, old);
		return false;
	}

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
	// While recording the render writes the lanes it reads, and plays none published: the walk after recording ends
	// publishes them all.
	const bool recording = playbackHandler.recording != RecordingMode::OFF;
	const bool settleAll = settle && g_liveUntilSettled.load(std::memory_order_relaxed) && !recording;
	if (currentSong == nullptr || g_publishing || recording || !mayPublish()
	    || (!g_anyPending.load(std::memory_order_relaxed) && !settleAll)) {
		return;
	}
	struct Walk {
		Walk() { g_publishing = true; }
		~Walk() {
			g_publishing = false;
			if (std::uncaught_exceptions() > 0) {
				g_anyPending.store(true, std::memory_order_relaxed); // the next publish point looks again
			}
		}
	} walk;
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
