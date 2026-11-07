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

#include "modulation/automation/working_copies.h"

#include "deluge-project/edit_guard.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "foundation/function_ref.h"
#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/record_events.h"
#include "modulation/params/lane_publication.h"
#include "modulation/params/lane_snapshot.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/staged_repeat.h"
#include "processing/engines/audio_engine.h"

#include <array>
#include <atomic>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t workingCopyOverflows = 0;
uint32_t latchWritesWithoutCopy = 0;
uint32_t renderTakeEnds = 0;
#endif

void noteLatchWithoutCopy() {
#if ALPHA_OR_BETA_VERSION
	latchWritesWithoutCopy++;
#endif
}

namespace {
/// One lane's working copy. An empty entry has no copy.
struct Entry {
	ParamCollection* collection = nullptr; ///< compared by address; reached only while it is alive
	uint32_t serial = 0;
	int32_t paramId = 0;
	project::DocAutoParam* copy = nullptr;
	bool written = false;   ///< [render] set, [interaction] cleared: the render changed the copy since the last commit
	Hold hold = Hold::TAKE; ///< [interaction]
	/// [render] or [interaction] set: the take ended for the copy; the render neither plays nor writes it, and the next
	/// commit copies what the render wrote into it into the model, then frees it.
	std::atomic<bool> ended = false;
	/// [render] or [interaction]: the repeats of its arrangement take (deluge::modulation::TakeRepeats) the copy holds
	/// the automation of. The render appends the next into a live copy at the take's loop end; the interaction tier
	/// appends each counted into a copy that does not hold it yet.
	uint32_t held = 0;
};

/// [interaction] -> [render] Changed only with the render held off, but for `written` and `ended`.
std::array<Entry, kWorkingCopies> g_entries;

/// [interaction] A take ended for some lanes since the commit last asked.
bool g_takeEnded = false;

void countOverflow() {
#if ALPHA_OR_BETA_VERSION
	workingCopyOverflows++;
#endif
}

void destroyCopy(void* copy) {
	static_cast<project::DocAutoParam*>(copy)->~DocAutoParam();
	memory::dealloc(copy);
}

/// A copy of @p model with room for @p room more nodes, or nullptr when out of memory.
project::DocAutoParam* copyOf(const project::DocAutoParam& model, size_t room) {
	void* memory = memory::alloc_sdram(sizeof(project::DocAutoParam), alignof(project::DocAutoParam));
	if (memory == nullptr) {
		return nullptr;
	}
	try {
		auto* copy = new (memory) project::DocAutoParam(model);
		if (copy->nodes.reserveExtra(static_cast<int32_t>(room))) {
			return copy;
		}
		copy->~DocAutoParam();
	} catch (...) {
		// out of memory copying the nodes
	}
	memory::dealloc(memory);
	return nullptr;
}

/// Call @p visit with the stack of each param manager @p song plays: its own, each clip's and each note row's.
void forEachOwner(Song& song, deluge::function_ref<void(ModelStackWithThreeMainThings&)> visit) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	visit(*song.setupModelStackWithSongAsTimelineCounter(memory));
	for (Clip* clip : AllClips::everywhere(&song)) {
		ModControllable* output = (clip->output != nullptr) ? clip->output->toModControllable() : nullptr;
		visit(*setupModelStackWithThreeMainThingsButNoNoteRow(memory, &song, output, clip, &clip->paramManager));
		if (clip->type != ClipType::INSTRUMENT) {
			continue;
		}
		auto* instrumentClip = static_cast<InstrumentClip*>(clip);
		for (int32_t i = 0; i < std::ssize(instrumentClip->noteRows); i++) {
			NoteRow& row = instrumentClip->noteRows[i];
			ModControllable* owner = (row.drum != nullptr) ? row.drum->toModControllable() : output;
			visit(*setupModelStackWithThreeMainThingsIncludingNoteRow(
			    memory, &song, clip, instrumentClip->getNoteRowId(&row, i), &row, owner, &row.paramManager));
		}
	}
}

/// The lane @p paramId of @p summary's collection, as a stack on @p owner.
ModelStackWithAutoParam* laneStack(ModelStackWithThreeMainThings& owner, ParamCollectionSummary& summary,
                                   int32_t paramId) {
	ParamCollection& collection = *summary.paramCollection;
	return collection.getAutoParamFromId(owner.addParamCollectionAndId(&collection, &summary, paramId));
}

/// Empty @p entry, with the render held off.
void clear(Entry& entry) {
	WorkingCopies::count(*entry.collection, -1);
	entry.collection = nullptr;
	entry.serial = 0;
	entry.paramId = 0;
	entry.copy = nullptr;
	entry.written = false;
	entry.hold = Hold::TAKE;
	entry.ended.store(false, std::memory_order_relaxed);
	entry.held = 0;
}

/// The entry holding the working copy of lane @p key of @p collection, live or ended, if any.
Entry* entryOf(const ParamCollection& collection, int32_t key) {
	if (!collection.hasWorkingCopies()) {
		return nullptr;
	}
	for (Entry& entry : g_entries) {
		if (entry.copy != nullptr && entry.collection == &collection && entry.paramId == key
		    && entry.serial == collection.serial()) {
			return &entry;
		}
	}
	return nullptr;
}

#if ALPHA_OR_BETA_VERSION
/// [render] Working copies whose take ended at an arrangement take's loop end because the render could not append the
/// repeat into them.
uint32_t g_copiesEndedAtLoopEnd = 0;
#endif

/// Whether @p entry holds a copy whose take goes on.
bool live(const Entry& entry) {
	return entry.copy != nullptr && !entry.ended.load(std::memory_order_relaxed);
}

/// Free the copies of the entries @p pick picks, each once the render cannot be reading it. With the render held
/// off it picks, and the entries are emptied; a collection whose ended copy is freed is marked for the next publish,
/// which plays its lanes from the model again. Out of memory, the picked entries are ended instead, and freed
/// by a later commit.
/// @return Whether an ended copy was freed.
template <typename Pick>
bool release(Pick pick) {
	// Room for every copy held: @p pick may read what the render writes, so it is asked only with the render held off
	const size_t held = workingCopiesHeld();
	if (held == 0) {
		return false;
	}
	bool reserved = true;
	try {
		AudioEngine::reserveRetirements(held);
	} catch (...) {
		reserved = false;
	}
	std::array<project::DocAutoParam*, kWorkingCopies> freed{};
	size_t count = 0;
	bool endedFreed = false;
	{
		AudioExcludedSection section;
		for (Entry& entry : g_entries) {
			if (entry.copy == nullptr || !pick(entry)) {
				continue;
			}
			if (!reserved) {
				entry.ended.store(true, std::memory_order_relaxed);
				continue;
			}
			if (entry.ended.load(std::memory_order_relaxed)) {
				endedFreed = true;
				markLanesChanged(*entry.collection);
			}
			freed[count++] = entry.copy;
			clear(entry);
		}
	}
	for (size_t i = 0; i < count; i++) {
		AudioEngine::retireMemory(freed[i], &destroyCopy);
	}
	return endedFreed;
}
} // namespace

void WorkingCopies::count(ParamCollection& collection, int32_t delta) {
	collection.workingCopies_ = static_cast<uint8_t>(collection.workingCopies_ + delta);
}

project::DocAutoParam* makeWorkingCopy(ModelStackWithAutoParam const& lane, size_t room, Hold hold) {
	ParamCollection& collection = *lane.paramCollection;
	if (project::DocAutoParam* existing = workingCopy(collection, lane.paramId)) {
		if (hold == Hold::TAKE) {
			for (Entry& entry : g_entries) {
				if (entry.copy == existing) {
					entry.hold = Hold::TAKE;
				}
			}
		}
		return existing;
	}
	Entry* slot = nullptr;
	for (Entry& entry : g_entries) {
		if (entry.copy == nullptr) {
			slot = &entry;
			break;
		}
	}
	if (slot == nullptr) {
		countOverflow();
		return nullptr;
	}
	// The take's undo restores the lane as it was before the render wrote anything into its copy
	if (Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED)) {
		action->recordParamChangeIfNotAlreadySnapshotted(&lane);
	}
	// A lane of an arrangement take: the model holds every repeat counted, so the copy does
	const TakeRepeats* repeats = takeRepeatsOf(lane);
	if (repeats != nullptr && repeats->inModel != repeats->appended.load(std::memory_order_acquire)) {
		catchUpArrangementTakes(*lane.song);
	}
	project::DocAutoParam* copy = copyOf(*lane.doc(), room);
	if (copy == nullptr) {
		countOverflow();
		return nullptr;
	}
	AudioExcludedSection section;
	slot->collection = &collection;
	slot->serial = collection.serial();
	slot->paramId = lane.paramId;
	slot->copy = copy;
	slot->hold = hold;
	slot->held = repeats != nullptr ? repeats->inModel : 0;
	WorkingCopies::count(collection, 1);
	return copy;
}

project::DocAutoParam* workingCopy(const ParamCollection& collection, int32_t paramId) {
	if (!collection.hasWorkingCopies()) {
		return nullptr;
	}
	for (const Entry& entry : g_entries) {
		if (entry.collection == &collection && entry.paramId == paramId && entry.serial == collection.serial()
		    && live(entry)) {
			return entry.copy;
		}
	}
	return nullptr;
}

void noteWorkingCopyWritten(const project::DocAutoParam& copy) {
	for (Entry& entry : g_entries) {
		if (entry.copy == &copy) {
			entry.written = true;
			return;
		}
	}
}

void commitWorkingCopies() {
	// Held while written, and not written since the last commit: released below unless the render writes it first
	std::array<bool, kWorkingCopies> idle{};
	{
		// The arrangement takes caught up first, with the render held off until the copies are committed: a committed
		// copy then holds the repeats the model counts, and no more
		AudioExcludedSection section;
		if (currentSong != nullptr) {
			catchUpArrangementTakes(*currentSong);
		}
		project::EditGuard guard{project::ModelEdit::NODE_KERNEL};
		for (size_t i = 0; i < g_entries.size(); i++) {
			Entry& entry = g_entries[i];
			// An ended copy the render wrote before its take ended is committed too: the render no longer writes it
			if (entry.copy == nullptr) {
				continue;
			}
			if (!entry.written) {
				idle[i] = live(entry) && entry.hold == Hold::WRITES;
				continue;
			}
			if (project::DocAutoParam* model = entry.collection->findDocParamForId(entry.paramId)) {
				*model = *entry.copy;
				markLanesChanged(*entry.collection);
			}
			entry.written = false;
		}
	}
	const Entry* first = g_entries.data();
	if (release([&](const Entry& entry) {
		    return entry.ended.load(std::memory_order_relaxed) || (idle[&entry - first] && !entry.written);
	    })) {
		g_takeEnded = true;
	}
}

void releaseWorkingCopies() {
	commitWorkingCopies();
	(void)release([](const Entry&) { return true; });
	g_takeEnded = true;
}

void endTake(ParamCollection& collection) {
	if (!collection.hasWorkingCopies()) {
		return;
	}
	if (memory::onRenderTier()) {
#if ALPHA_OR_BETA_VERSION
		renderTakeEnds++;
#endif
		for (Entry& entry : g_entries) {
			if (entry.collection == &collection && live(entry)) {
				entry.ended.store(true, std::memory_order_relaxed);
			}
		}
		return;
	}
	applyRecordQueue();
	commitWorkingCopies();
	(void)release([&](const Entry& entry) { return entry.collection == &collection; });
	g_takeEnded = true;
}

bool takeEndedSinceLastAsked() {
	return std::exchange(g_takeEnded, false);
}

void dropWorkingCopies(ParamCollection& collection) {
	if (!collection.hasWorkingCopies()) {
		return;
	}
	std::optional<AudioExcludedSection> section;
	if (!memory::onRenderTier()) {
		section.emplace();
	}
	for (Entry& entry : g_entries) {
		if (entry.collection == &collection) {
			project::DocAutoParam* copy = entry.copy;
			clear(entry);
			destroyCopy(copy);
		}
	}
}

void copyOverriddenLanes(Song& song) {
	// Which lanes are overriding is the render's cursor state: read with it held off, one owner at a time; the copies
	// are made after.
	struct Overridden {
		ParamCollectionSummary* summary;
		int32_t paramId;
	};
	std::vector<Overridden> overridden;
	overridden.reserve(kWorkingCopies);
	forEachOwner(song, [&](ModelStackWithThreeMainThings& owner) {
		overridden.clear();
		{
			AudioExcludedSection section;
			for (ParamCollectionSummary& summary : owner.paramManager->summaries) {
				if (summary.paramCollection == nullptr) {
					break;
				}
				summary.paramCollection->forEachLane([&](int32_t paramId, const project::DocAutoParam&) {
					ModelStackWithAutoParam* lane = laneStack(owner, summary, paramId);
					if (lane != nullptr && lane->autoParam != nullptr
					    && lane->autoParam->renewedOverridingAtTime != 0) {
						overridden.push_back({&summary, paramId});
					}
				});
			}
		}
		for (const Overridden& lane : overridden) {
			if (ModelStackWithAutoParam* stack = laneStack(owner, *lane.summary, lane.paramId);
			    stack != nullptr && stack->autoParam != nullptr) {
				(void)makeWorkingCopy(*stack, kNodesPerRecordedWrite);
			}
		}
	});
}

bool appendRepeatToCopy(ParamCollection& collection, int32_t key, uint32_t repeat, const project::DocAutoParam* src,
                        const RepeatArgs& args) {
	Entry* entry = entryOf(collection, key);
	if (entry == nullptr) {
		return false;
	}
	if (entry->held == repeat) {
		if (src != nullptr && !src->nodes.empty()) {
			// A live copy is the render's while it plays: written with it held off
			std::optional<AudioExcludedSection> section;
			if (live(*entry)) {
				section.emplace();
			}
			project::appendParam(*entry->copy, *src, args.offset, args.reverseThisRepeatWithLength,
			                     args.pingpongingGenerally);
			entry->written = true;
		}
		entry->held++;
	}
	return true;
}

void appendRepeatIntoCopies(ParamCollection& collection, ParamCollection& source,
                            const ParamCollectionSummary& sourceSummary, uint32_t repeat, const RepeatArgs& args) {
	if (!collection.hasWorkingCopies()) {
		return;
	}
	const LaneSnapshot* published = source.published();
	collection.forEachAppendedKey(source, sourceSummary, [&](int32_t key) {
		Entry* entry = entryOf(collection, key);
		if (entry == nullptr || !live(*entry)) {
			return;
		}
		const PublishedLane* src = published != nullptr ? published->find(key) : nullptr;
		const size_t room = src != nullptr ? project::appendParamRoom(src->count) : 0;
		project::DocAutoParam& copy = *entry->copy;
		if (entry->held != repeat || published == nullptr || copy.nodes.capacity() < copy.nodes.size() + room) {
			// Behind the take, its source lane not published, or no room: the take ends for it, as for lanes edited
			// all at once, and the next commit carries it into the model, which the repeat is appended to
			entry->ended.store(true, std::memory_order_relaxed);
#if ALPHA_OR_BETA_VERSION
			g_copiesEndedAtLoopEnd++;
#endif
			return;
		}
		if (src != nullptr) {
			project::appendParam(copy, src->view(), args.offset, args.reverseThisRepeatWithLength,
			                     args.pingpongingGenerally);
			entry->written = true;
		}
		entry->held++;
	});
}

void reserveRepeatInCopies(ParamCollection& collection, ParamCollection& source,
                           const ParamCollectionSummary& sourceSummary) {
	if (!collection.hasWorkingCopies()) {
		return;
	}
	const LaneSnapshot* published = source.published();
	collection.forEachAppendedKey(source, sourceSummary, [&](int32_t key) {
		Entry* entry = entryOf(collection, key);
		const PublishedLane* src = published != nullptr ? published->find(key) : nullptr;
		if (entry == nullptr || !live(*entry) || src == nullptr) {
			return;
		}
		// Room for the repeat beside the room the copy keeps for recorded writes
		const size_t room = project::appendParamRoom(src->count) + kNodesPerRecordedWrite;
		AudioExcludedSection section;
		if (entry->copy->nodes.capacity() < entry->copy->nodes.size() + room) {
			(void)entry->copy->nodes.reserveExtra(static_cast<int32_t>(room));
		}
	});
}

uint32_t copiesEndedAtLoopEnd() {
#if ALPHA_OR_BETA_VERSION
	return g_copiesEndedAtLoopEnd;
#else
	return 0;
#endif
}

size_t workingCopiesHeld() {
	size_t held = 0;
	for (const Entry& entry : g_entries) {
		held += (entry.copy != nullptr) ? 1 : 0;
	}
	return held;
}

} // namespace deluge::modulation
