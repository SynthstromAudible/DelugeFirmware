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
#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "modulation/params/lane_publication.h"
#include "modulation/params/param_collection.h"
#include "processing/engines/audio_engine.h"

#include <array>
#include <new>
#include <optional>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t workingCopyOverflows = 0;
#endif

namespace {
/// One lane's working copy. An empty entry has no copy.
struct Entry {
	ParamCollection* collection = nullptr; ///< compared by address; reached only while it is alive
	uint32_t serial = 0;
	int32_t paramId = 0;
	project::DocAutoParam* copy = nullptr;
	bool written = false; ///< [render] set, [interaction] cleared: the render changed the copy since the last commit
};

/// [interaction] -> [render] Changed only with the render held off.
std::array<Entry, kWorkingCopies> g_entries;

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

/// Empty @p entry, with the render held off.
void clear(Entry& entry) {
	WorkingCopies::count(*entry.collection, -1);
	entry = {};
}
} // namespace

void WorkingCopies::count(ParamCollection& collection, int32_t delta) {
	collection.workingCopies_ = static_cast<uint8_t>(collection.workingCopies_ + delta);
}

project::DocAutoParam* makeWorkingCopy(ModelStackWithAutoParam const& lane, size_t room) {
	ParamCollection& collection = *lane.paramCollection;
	if (project::DocAutoParam* existing = workingCopy(collection, lane.paramId)) {
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
	*slot = {.collection = &collection, .serial = collection.serial(), .paramId = lane.paramId, .copy = copy};
	WorkingCopies::count(collection, 1);
	return copy;
}

project::DocAutoParam* workingCopy(const ParamCollection& collection, int32_t paramId) {
	if (!collection.hasWorkingCopies()) {
		return nullptr;
	}
	for (const Entry& entry : g_entries) {
		if (entry.collection == &collection && entry.paramId == paramId && entry.serial == collection.serial()) {
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
	project::EditGuard guard{project::ModelEdit::NODE_KERNEL};
	for (Entry& entry : g_entries) {
		if (entry.copy == nullptr || !entry.written) {
			continue;
		}
		if (project::DocAutoParam* model = entry.collection->findDocParamForId(entry.paramId)) {
			*model = *entry.copy;
			markLanesChanged(*entry.collection);
		}
		entry.written = false;
	}
}

void releaseWorkingCopies() {
	commitWorkingCopies();
	try {
		AudioEngine::reserveRetirements(workingCopiesHeld());
	} catch (...) {
		return; // out of memory: they stay installed, committed, and are released at the next call
	}
	for (Entry& entry : g_entries) {
		if (entry.copy == nullptr) {
			continue;
		}
		project::DocAutoParam* copy = entry.copy;
		{
			AudioExcludedSection section;
			clear(entry);
		}
		AudioEngine::retireMemory(copy, &destroyCopy);
	}
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

size_t workingCopiesHeld() {
	size_t held = 0;
	for (const Entry& entry : g_entries) {
		held += (entry.copy != nullptr) ? 1 : 0;
	}
	return held;
}

} // namespace deluge::modulation
