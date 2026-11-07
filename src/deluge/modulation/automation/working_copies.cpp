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
#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_manager.h"
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
	/// [render] or [interaction] set: the take ended for the copy; neither tier plays, writes or commits it, and the
	/// next commit frees it.
	std::atomic<bool> ended = false;
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
}

/// Whether @p entry holds a copy whose take goes on.
bool live(const Entry& entry) {
	return entry.copy != nullptr && !entry.ended.load(std::memory_order_relaxed);
}

/// Free the copies of the entries @p pick picks, each once the render cannot be reading it. With the render held
/// off it picks, and the entries are emptied; a collection whose ended copy is freed is marked for the next publish,
/// which carries what the render copied into its model. Out of memory, the picked entries are ended instead, and freed
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
		project::EditGuard guard{project::ModelEdit::NODE_KERNEL};
		for (size_t i = 0; i < g_entries.size(); i++) {
			Entry& entry = g_entries[i];
			if (!live(entry)) {
				continue;
			}
			if (!entry.written) {
				idle[i] = entry.hold == Hold::WRITES;
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
		for (Entry& entry : g_entries) {
			if (entry.collection != &collection || !live(entry)) {
				continue;
			}
			if (entry.written) {
				if (project::DocAutoParam* model = collection.findDocParamForId(entry.paramId)) {
					*model = *entry.copy;
					noteRenderLaneWrite();
				}
			}
			entry.ended.store(true, std::memory_order_relaxed);
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

size_t workingCopiesHeld() {
	size_t held = 0;
	for (const Entry& entry : g_entries) {
		held += (entry.copy != nullptr) ? 1 : 0;
	}
	return held;
}

} // namespace deluge::modulation
