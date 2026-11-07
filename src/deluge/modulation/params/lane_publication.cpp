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
#include "modulation/params/staged_repeat.h"
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
uint32_t renderModelWrites = 0;
uint32_t laneRequestOverflows = 0;
uint32_t renderMadeCollections = 0;
#endif

namespace {
/// Whether a collection was marked or made since the last walk of the song (written by either tier).
std::atomic<bool> g_anyPending{false};
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

/// Whether @p kept holds @p entry's lane itself: a lane shared between snapshots is the same object under the same key.
bool shares(const LaneSnapshot* kept, const LaneSnapshot::Entry& entry) {
	return kept != nullptr && kept->find(entry.key) == entry.lane;
}

/// Free a snapshot never published, @p next, and the lanes it was built with (@p built) that @p old does not share.
void discard(std::span<const LaneSnapshot::Entry> built, LaneSnapshot* next, const LaneSnapshot* old) {
	for (const LaneSnapshot::Entry& entry : built) {
		if (!shares(old, entry)) {
			PublishedLane::destroy(const_cast<PublishedLane*>(entry.lane));
		}
	}
	LaneSnapshot::destroy(next);
}

/// The retireMemory() calls retiring @p snapshot and its lanes @p kept does not share takes.
size_t retirementsFor(const LaneSnapshot& snapshot, const LaneSnapshot* kept) {
	return 1 + std::ranges::count_if(snapshot.entries(), [&](const LaneSnapshot::Entry& entry) {
		       return !shares(kept, entry);
	       });
}

/// Retire @p snapshot and its lanes @p kept does not share, with room already reserved for them.
void retireReserved(const LaneSnapshot& snapshot, const LaneSnapshot* kept) {
	for (const LaneSnapshot::Entry& entry : snapshot.entries()) {
		if (!shares(kept, entry)) {
			AudioEngine::retireMemory(const_cast<PublishedLane*>(entry.lane), &PublishedLane::destroy);
		}
	}
	AudioEngine::retireMemory(const_cast<LaneSnapshot*>(&snapshot), &LaneSnapshot::destroy);
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

void noteRenderModelRead() {
#if ALPHA_OR_BETA_VERSION
	if (deluge::memory::onRenderTier()) {
		renderModelReads++;
	}
#endif
}

const LaneSnapshot* snapshotToPlay(const ParamCollection& collection) {
	return deluge::memory::onRenderTier() ? collection.published() : nullptr;
}

void noteRenderLaneWrite() {
#if ALPHA_OR_BETA_VERSION
	if (deluge::memory::onRenderTier()) {
		renderModelWrites++;
	}
#endif
}

void LanePublication::made(ParamCollection& /*collection*/) {
#if ALPHA_OR_BETA_VERSION
	if (deluge::memory::onRenderTier()) {
		renderMadeCollections++;
	}
#endif
	g_anyPending.store(true, std::memory_order_relaxed);
}

void LanePublication::gone(ParamCollection& collection) {
	// Freed now, not retired: the render reaches a snapshot only through its collection, within a block, and a
	// collection being destroyed is already out of the render's reach (or the collection itself would be read freed).
	// Retiring would also hold back a load's wait for the outgoing song, whose collections go when it is freed.
	// The staged and superseded snapshots share lanes with the published one, and the superseded one with the staged:
	// each lane is freed with the first snapshot holding it.
	const LaneSnapshot* published = collection.published_.exchange(nullptr);
	const LaneSnapshot* staged = collection.staged_.exchange(nullptr);
	const LaneSnapshot* superseded = collection.superseded_.exchange(nullptr);
	if (published != nullptr) {
		discardSnapshot(published, nullptr);
	}
	if (staged != nullptr) {
		discardSnapshot(staged, published);
	}
	if (superseded != nullptr) {
		for (const LaneSnapshot::Entry& entry : superseded->entries()) {
			if (!shares(published, entry) && !shares(staged, entry)) {
				PublishedLane::destroy(const_cast<PublishedLane*>(entry.lane));
			}
		}
		LaneSnapshot::destroy(const_cast<LaneSnapshot*>(superseded));
	}
}

bool LanePublication::stagedAgainst(const ParamCollection& collection, const LaneSnapshot* sourcePublished) {
	return collection.staged_.load(std::memory_order_relaxed) != nullptr
	       && collection.stagedAgainst_ == sourcePublished;
}

bool LanePublication::stage(ParamCollection& collection, LaneSource lanes, const LaneSnapshot* sourcePublished) {
	if (!settleStaged(collection)) {
		return false;
	}
	// Nothing is staged now, so the render cannot swap: the published snapshot stays as it is while this builds
	LaneSnapshot* staged = buildSnapshot(lanes, collection.published());
	if (staged == nullptr) {
		return false;
	}
	collection.stagedAgainst_ = sourcePublished;
	collection.staged_.store(staged, std::memory_order_release);
	return true;
}

bool LanePublication::canSwap(const ParamCollection& collection) {
	return collection.staged_.load(std::memory_order_acquire) != nullptr
	       && collection.superseded_.load(std::memory_order_relaxed) == nullptr;
}

void LanePublication::swap(ParamCollection& collection) {
	const LaneSnapshot* staged = collection.staged_.exchange(nullptr, std::memory_order_acq_rel);
	collection.superseded_.store(collection.published_.exchange(staged, std::memory_order_acq_rel),
	                             std::memory_order_release);
}

bool LanePublication::settleStaged(ParamCollection& collection) {
	if (collection.staged_.load(std::memory_order_relaxed) == nullptr
	    && collection.superseded_.load(std::memory_order_relaxed) == nullptr) {
		return true;
	}
	const LaneSnapshot* staged = nullptr;
	const LaneSnapshot* superseded = nullptr;
	{
		// Taken with the render held off, so it cannot swap one in between: the published snapshot is then stable
		AudioExcludedSection masked;
		staged = collection.staged_.exchange(nullptr);
		superseded = collection.superseded_.exchange(nullptr);
	}
	const LaneSnapshot* published = collection.published();
	if (superseded != nullptr && !retireSnapshot(superseded, published)) {
		AudioExcludedSection masked;
		collection.superseded_.store(superseded);
		collection.staged_.store(staged);
		return false;
	}
	if (staged != nullptr) {
		discardSnapshot(staged, published);
	}
	return true;
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

LaneSnapshot* buildSnapshot(LaneSource lanes, const LaneSnapshot* old) {
	uint32_t count = 0;
	lanes([&](int32_t, const project::DocAutoParam&) { count++; });
	LaneSnapshot* next = LaneSnapshot::make(count);
	if (next == nullptr) {
		return nullptr;
	}
	// Each lane the old snapshot holds unchanged is shared; the rest are copied
	std::span<LaneSnapshot::Entry> entries = next->entriesToFill();
	uint32_t filled = 0;
	bool outOfMemory = false;
	lanes([&](int32_t key, const project::DocAutoParam& lane) {
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
		return nullptr;
	}
	std::ranges::sort(entries, {}, &LaneSnapshot::Entry::key);
	return next;
}

void discardSnapshot(const LaneSnapshot* snapshot, const LaneSnapshot* kept) {
	discard(snapshot->entries(), const_cast<LaneSnapshot*>(snapshot), kept);
}

bool retireSnapshot(const LaneSnapshot* snapshot, const LaneSnapshot* kept) {
	try {
		AudioEngine::reserveRetirements(retirementsFor(*snapshot, kept));
	} catch (...) {
		return false;
	}
	retireReserved(*snapshot, kept);
	return true;
}

bool LanePublication::publish(ParamCollection& collection, ParamManagerForTimeline& manager) {
	// The staged and superseded snapshots share lanes with the published one, which this retires
	if (!settleStaged(collection)) {
		return false;
	}
	if (collection.published() != nullptr && matches(collection)) {
		return true;
	}
	const LaneSnapshot* old = collection.published();
	LaneSnapshot* next = buildSnapshot([&](LaneVisit visit) { collection.forEachLane(visit); }, old);
	if (next == nullptr) {
		return false;
	}

	// Room to retire what the new snapshot replaces, made before the swap so retiring it cannot fail
	try {
		AudioEngine::reserveRetirements(old != nullptr ? retirementsFor(*old, next) : 0);
	} catch (...) {
		discardSnapshot(next, old);
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
		retireReserved(*old, next);
	}
	return true;
}

void LanePublication::publishDirty(bool settle) {
	if (currentSong == nullptr || g_publishing || !mayPublish()) {
		return;
	}
	walkDirty(settle);
	if (settle) {
		// A job's end: the arrangement takes' next repeats are staged from what it published
		restageArrangementTakes(*currentSong);
	}
}

void LanePublication::walkDirty(bool settle) {
	if (!g_anyPending.load(std::memory_order_relaxed)) {
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
		if (!collection.lanesDirty_ && collection.published() != nullptr) {
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

bool LanePublication::publishManager(ParamManagerForTimeline& manager) {
	if (!mayPublish()) {
		return false;
	}
	bool published = true;
	for (ParamCollectionSummary& summary : manager.summaries) {
		if (summary.paramCollection == nullptr) {
			break;
		}
		published = publish(*summary.paramCollection, manager) && published;
	}
	return published;
}

int32_t LanePublication::stagedLeft(Song& song) {
	int32_t left = 0;
	forEachPlayedCollection(song, [&](ParamCollection& collection, ParamManagerForTimeline&) {
		left += (collection.staged_.load(std::memory_order_relaxed) != nullptr
		         || collection.superseded_.load(std::memory_order_relaxed) != nullptr)
		            ? 1
		            : 0;
	});
	return left;
}

bool publishParamManager(ParamManagerForTimeline& manager) {
	return LanePublication::publishManager(manager);
}

int32_t stagedSnapshotsLeft(Song& song) {
	return LanePublication::stagedLeft(song);
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
