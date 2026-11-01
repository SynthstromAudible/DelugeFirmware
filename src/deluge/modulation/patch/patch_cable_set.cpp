/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/patch/patch_cable_set.h"
#include "definitions_cxx.hpp"
#include "deluge-project/edit_guard.h"
#include "deluge-project/param_automation.h"
#include "gui/views/view.h"
#include "io/midi/midi_device.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "modulation/patch/patch_cable.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/mod_matrix_host.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "synth/matrix/compiler.h"
#include "util/exceptions.h"
#include "util/misc.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <bitset>
#include <libdeluge/system.h>
#include <new>
#include <string.h>

namespace params = deluge::modulation::params;
using deluge::project::EditGuard;
using deluge::project::ModelEdit;

void flagCable(uint32_t* flags, int32_t c) {
	int32_t idx;
	if constexpr (kNumUnsignedIntegersToRepPatchCables > 1) {
		idx = c >> 5;
	}
	else {
		idx = 0;
	}
	flags[idx] |= ((uint32_t)1 << (c & 31));
}

void unflagCable(uint32_t* flags, int32_t c) {
	int32_t idx;
	if constexpr (kNumUnsignedIntegersToRepPatchCables > 1) {
		idx = c >> 5;
	}
	else {
		idx = 0;
	}
	flags[idx] &= ~((uint32_t)1 << (c & 31));
}

inline void PatchCableSet::freeDestinationMemory(bool destructing) {
	for (int32_t g = 0; g < 2; g++) {
		if (destinations[g]) {
			delugeDealloc(destinations[g]);
			if (!destructing) {
				destinations[g] = nullptr;
			}
		}
	}
	if (program_ != nullptr) {
		delugeDealloc(const_cast<deluge::synth::matrix::MatrixProgram*>(program_));
		if (!destructing) {
			program_ = nullptr;
		}
	}
	if (routedCables_ != nullptr) {
		delugeDealloc(routedCables_);
		if (!destructing) {
			routedCables_ = nullptr;
		}
	}
}

PatchCableSet::PatchCableSet(ParamCollectionSummary* summary) : ParamCollection(sizeof(PatchCableSet), summary) {
}

PatchCableSet::~PatchCableSet() {
	freeDestinationMemory(true);
	for (int32_t c = 0; c < numPatchCables; c++) {
		patchCables[c].~PatchCable();
	}
	deluge::memory::dealloc(patchCables);
}

bool PatchCableSet::resizeCables(uint8_t count) {
	const uint8_t oldCount = numPatchCables;
	if (count == oldCount) {
		return true;
	}
	PatchCable* storage = nullptr;
	if (count != 0) {
		storage = static_cast<PatchCable*>(deluge::memory::alloc_fast(sizeof(PatchCable) * count, alignof(PatchCable)));
		if (storage == nullptr && count > oldCount) {
			return false;
		}
	}
	if (storage == nullptr && count != 0) {
		// A shrink with no memory for the smaller storage drops the cables where they are: it never fails.
		{
			AudioExcludedSection section;
			deluge::noteStructuralEdit(deluge::StructuralContainer::PATCH_CABLES);
			if (count < numUsablePatchCables) {
				freeDestinationMemory(false);
				sourcesPatchedToAnything[GLOBALITY_LOCAL] = 0;
				sourcesPatchedToAnything[GLOBALITY_GLOBAL] = 0;
				numUsablePatchCables = count;
			}
			numPatchCables = count;
		}
		for (int32_t c = count; c < oldCount; c++) {
			patchCables[c].~PatchCable();
		}
		return true;
	}
	PatchCable* old = patchCables;
	const uint8_t kept = std::min(count, oldCount);
	{
		// The render walks these cables and ticks their amounts, so they move and the new storage is published with it
		// excluded: it sees all of the old storage or all of the new.
		AudioExcludedSection section;
		deluge::noteStructuralEdit(deluge::StructuralContainer::PATCH_CABLES);
		if (kept != 0) {
			memcpy(static_cast<void*>(storage), old,
			       sizeof(PatchCable) * kept); // a move: the old copies are not destroyed
		}
		for (int32_t c = kept; c < count; c++) {
			new (&storage[c]) PatchCable();
		}
		if (count < numUsablePatchCables) {
			// The Destinations may refer to a dropped cable. No patching until setupPatching() runs again.
			freeDestinationMemory(false);
			sourcesPatchedToAnything[GLOBALITY_LOCAL] = 0;
			sourcesPatchedToAnything[GLOBALITY_GLOBAL] = 0;
			numUsablePatchCables = count;
		}
		patchCables = storage;
		numPatchCables = count;
	}
	for (int32_t c = kept; c < oldCount; c++) {
		old[c].~PatchCable();
	}
	deluge::memory::dealloc(old);
	return true;
}

PatchCable* PatchCableSet::addCable() {
	if (numPatchCables >= kMaxNumPatchCables || !resizeCables(numPatchCables + 1)) {
		return nullptr;
	}
	return &patchCables[numPatchCables - 1];
}

void PatchCableSet::truncateCables(uint8_t count) {
	count = std::min(count, numPatchCables);
	// A dropped cable's lane goes with it, unless a kept cable has the same identity (a removal moves the last cable
	// over the removed one, so for a moment both slots hold it).
	for (int32_t c = count; c < numPatchCables; c++) {
		const auto same = [&](const PatchCable& kept) {
			return kept.from == patchCables[c].from
			       && kept.destinationParamDescriptor == patchCables[c].destinationParamDescriptor;
		};
		if (std::none_of(patchCables, patchCables + count, same)) {
			eraseLane(patchCables[c].from, patchCables[c].destinationParamDescriptor);
		}
	}
	(void)resizeCables(count); // a shrink never fails
	records_.truncate(count);
}

PatchCable* PatchCableSet::appendCable(const deluge::synth::CableRecord& record) {
	PatchCable* cable = addCable();
	if (cable == nullptr) {
		return nullptr;
	}
	records_.append(record);
	cable->from = record.source;
	cable->destinationParamDescriptor = ParamDescriptor(static_cast<int32_t>(record.target.word));
	cable->polarity = static_cast<Polarity>(record.polarity);
	cable->param.currentValue = 0;
	// Every cable has its lane from the start, so a lookup never grows the table under a lane already handed out.
	laneFor(record.source, cable->destinationParamDescriptor) = {};
	return cable;
}

PatchCable* PatchCableSet::appendDefaultCable(PatchSource from, uint8_t to, int32_t amount) {
	PatchCable* cable = appendCable({from, deluge::synth::CableTarget::param(to),
	                                 static_cast<deluge::synth::CablePolarity>(PatchCable::getDefaultPolarity(from))});
	if (cable != nullptr) {
		cable->param.currentValue = amount;
		docForParam(numPatchCables - 1)->current_value = amount;
	}
	return cable;
}

bool PatchCableSet::replaceRecords(Sound& sound, ParamManager& paramManager,
                                   const deluge::instrument::CableRecords& records, const CableDepth* restore) {
	struct Kept {
		deluge::synth::CableRecord record;
		int32_t value;
		deluge::project::DocAutoParam lane;
	};
	std::vector<Kept> kept;
	kept.reserve(numPatchCables + 1);
	for (int32_t c = 0; c < numPatchCables; c++) {
		kept.push_back({records_[c], patchCables[c].param.currentValue, *docForParam(c)});
	}
	if (restore != nullptr) {
		kept.push_back({restore->cable, restore->lane.current_value, restore->lane});
	}
	bool complete = true;
	{
		// The cables are rebuilt and placed in one step: the render never sees the set half-built.
		AudioExcludedSection section;
		truncateCables(0);
		for (const deluge::synth::CableRecord& record : records.records()) {
			PatchCable* cable = appendCable(record);
			if (cable == nullptr) {
				complete = false;
				break;
			}
			const auto same = [&](const Kept& k) {
				return k.record.source == record.source && k.record.target == record.target;
			};
			// The last match wins, so a lane being restored replaces the one the cable had.
			if (const auto it = std::ranges::find_last_if(kept, same).begin(); it != kept.end()) {
				cable->param.currentValue = it->value;
				*docForParam(numPatchCables - 1) = it->lane;
			}
		}
		setupPatching(sound, paramManager);
	}
	return complete;
}

void PatchCableSet::retarget(uint8_t c, deluge::synth::CableTarget target) {
	const deluge::project::DocAutoParam lane = *docForParam(c);
	eraseLane(patchCables[c].from, patchCables[c].destinationParamDescriptor);
	records_.set_target(c, target);
	patchCables[c].destinationParamDescriptor = ParamDescriptor(static_cast<int32_t>(target.word));
	laneFor(patchCables[c].from, patchCables[c].destinationParamDescriptor) = lane;
}

void PatchCableSet::setPolarity(uint8_t c, Polarity polarity) {
	records_.set_polarity(c, static_cast<deluge::synth::CablePolarity>(polarity));
	patchCables[c].polarity = polarity;
}

void PatchCableSet::permuteCables(std::span<const uint8_t> order) {
	records_.permute(order);
	// cur[c]: the original index of the cable now at c; where[o]: where original cable o now is.
	std::array<uint8_t, kMaxNumPatchCables> cur{};
	std::array<uint8_t, kMaxNumPatchCables> where{};
	for (size_t c = 0; c < order.size(); c++) {
		cur[c] = where[c] = static_cast<uint8_t>(c);
	}
	for (size_t c = 0; c < order.size(); c++) {
		const uint8_t p = where[order[c]];
		if (p == c) {
			continue;
		}
		swapCables(static_cast<int32_t>(c), p);
		const uint8_t displaced = cur[c];
		cur[p] = displaced;
		where[displaced] = p;
		cur[c] = order[c];
		where[order[c]] = static_cast<uint8_t>(c);
	}
}

deluge::project::DocAutoParam& PatchCableSet::laneFor(PatchSource from, ParamDescriptor target) {
	const deluge::plugin::ParamId id = deluge::bridge::cable_depth(from, target);
	std::vector<deluge::project::DocCableDepth>& table = depths();
	for (deluge::project::DocCableDepth& lane : table) {
		if (lane.depth == id) {
			return lane.amount;
		}
	}
	// The render scans these lanes to resolve a cable's depth: a growth may move them.
	EditGuard guard{ModelEdit::PARAM_STORE};
	table.push_back({.depth = id, .amount = {}});
	return table.back().amount;
}

void PatchCableSet::eraseLane(PatchSource from, ParamDescriptor target) {
	const deluge::plugin::ParamId id = deluge::bridge::cable_depth(from, target);
	// The lane's nodes are moved out inside the section and freed after it.
	deluge::project::DocAutoParam dropped;
	EditGuard guard{ModelEdit::PARAM_STORE};
	std::vector<deluge::project::DocCableDepth>& table = depths();
	auto lane = std::ranges::find(table, id, &deluge::project::DocCableDepth::depth);
	if (lane != table.end()) {
		dropped = std::move(lane->amount);
		table.erase(lane);
	}
}

deluge::project::DocAutoParam* PatchCableSet::docForParam(int32_t c) {
	return &laneFor(patchCables[c].from, patchCables[c].destinationParamDescriptor);
}

void PatchCableSet::relocateDepthsInto(std::vector<deluge::project::DocCableDepth>& slot) {
	std::vector<deluge::project::DocCableDepth>& current = depths();
	if (&current == &slot) {
		return;
	}
	EditGuard guard{ModelEdit::PARAM_STORE};
	slot = std::move(current);
	current.clear();
	externalDepths_ = &slot;
}

deluge::project::DocAutoParam* PatchCableSet::docParamForId(int32_t paramId) {
	PatchSource s;
	ParamDescriptor desc;
	dissectParamId(paramId, &desc, &s);
	uint8_t c = getPatchCableIndex(s, desc);
	return (c == 255) ? nullptr : docForParam(c);
}

deluge::project::DocAutoParam* PatchCableSet::findDocParamForId(int32_t paramId) {
	PatchSource s;
	ParamDescriptor desc;
	dissectParamId(paramId, &desc, &s);
	if (getPatchCableIndex(s, desc) == 255) {
		return nullptr;
	}
	const deluge::plugin::ParamId id = deluge::bridge::cable_depth(s, desc);
	for (deluge::project::DocCableDepth& lane : depths()) {
		if (lane.depth == id) {
			return &lane.amount;
		}
	}
	return nullptr;
}

bool PatchCableSet::isSourcePatchedToSomething(PatchSource s) {
	uint32_t sourcesPatchedToSomething =
	    sourcesPatchedToAnything[GLOBALITY_LOCAL] | sourcesPatchedToAnything[GLOBALITY_GLOBAL];
	return sourcesPatchedToSomething & (1 << util::to_underlying(s));
}

// To be called when setupPatching() hasn't been called yet
bool PatchCableSet::isSourcePatchedToSomethingManuallyCheckCables(PatchSource s) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		if (patchCables[c].from == s) {
			return true;
		}
	}

	return false;
}

bool PatchCableSet::doesParamHaveSomethingPatchedToIt(int32_t p) {
	return getDestinationForParam(p);
}

void PatchCableSet::swapCables(int32_t c1, int32_t c2) {
	char temp[sizeof(PatchCable)];

	memcpy(&temp, &patchCables[c1], sizeof(PatchCable));
	memcpy(&patchCables[c1], &patchCables[c2], sizeof(PatchCable));
	memcpy(&patchCables[c2], &temp, sizeof(PatchCable));
}

deluge::synth::RoutedDestination* PatchCableSet::getDestinationForParam(int32_t p) {
	int32_t globality = (p < params::FIRST_GLOBAL) ? GLOBALITY_LOCAL : GLOBALITY_GLOBAL;

	// Special case - no Destinations at all
	if (!destinations[globality]) {
		return nullptr;
	}

	const deluge::synth::CableTarget target = deluge::synth::CableTarget::param(static_cast<uint8_t>(p));

	// TODO: don't just linear search. But then, there won't be more than like 4 Destinations usually....
	for (deluge::synth::RoutedDestination* destination = &destinations[globality][0]; destination->sources;
	     destination++) {
		if (destination->target == target) {
			return destination;
		}
	}
	return nullptr;
}

extern int32_t rangeFinalValues[];

void PatchCableSet::setupPatching(Sound& sound, ParamManager& paramManager) {
	const auto usable =
	    deluge::synth::usability_of(records_.records(), [&](PatchSource s, deluge::synth::CableTarget target) {
		    // A leftover range placeholder classifies as a global param, past the end of the global params.
		    if (target == deluge::synth::CableTarget::param(params::PLACEHOLDER_RANGE)) {
			    return false;
		    }
		    return sound.maySourcePatchToParam(s, target.param_index(), &paramManager) == PatchCableAcceptance::ALLOWED;
	    });
	setupPatching(*paramManager.getPatchCableSetSummary(), usable);
}

void PatchCableSet::setupPatching(ParamCollectionSummary& summary, std::bitset<kMaxNumPatchCables> usable) {
	// The render walks the cables by the Destinations' runs: reordering the one and rebuilding the other must not
	// interleave with it.
	AudioExcludedSection section;
	deluge::noteStructuralEdit(deluge::StructuralContainer::PATCH_CABLES);

	freeDestinationMemory(false);
	for (int32_t g = 0; g < 2; g++) {
		destinations[g] = static_cast<deluge::synth::RoutedDestination*>(
		    deluge::memory::alloc_fast(sizeof(deluge::synth::RoutedDestination) * (kMaxNumPatchCables + 1)));
		if (!destinations[g]) {
			if (g == 1) {
				delugeDealloc(destinations[0]);
				destinations[0] = nullptr;
			}
			return;
		}
	}
	if (numPatchCables != 0) {
		routedCables_ = static_cast<deluge::synth::RoutedCable*>(
		    deluge::memory::alloc_fast(sizeof(deluge::synth::RoutedCable) * numPatchCables));
		if (routedCables_ == nullptr) {
			freeDestinationMemory(false);
			return;
		}
	}

	// The compile is the main thread's alone, and never nests.
	static deluge::synth::RoutingBuild build;
	deluge::synth::compile_routing(records_.records(), usable, build);
	if constexpr (ALPHA_OR_BETA_VERSION) {
		if (build.missing_range_link) {
			FREEZE_WITH_ERROR("E434");
		}
	}
	// The matrix program, compiled from the same routing; without it the cables cannot play, so none are installed
	const std::size_t program_bytes = deluge::synth::matrix::program_size(build);
	void* program_block = deluge::memory::alloc_fast(program_bytes, alignof(deluge::synth::matrix::MatrixProgram));
	if (program_block == nullptr) {
		freeDestinationMemory(false);
		return;
	}
	program_ = deluge::synth::matrix::compile_program(build, {static_cast<std::byte*>(program_block), program_bytes});

	permuteCables(std::span{build.order}.first(build.num_cables));
	numUsablePatchCables = build.num_usable;

	std::copy_n(build.cables.begin(), numPatchCables, routedCables_);
	for (int32_t g = 0; g < 2; g++) {
		sourcesPatchedToAnything[g] = build.sources_patched[g];
		const int32_t count = build.num_destinations[g];
		if (count == 0) {
			delugeDealloc(destinations[g]);
			destinations[g] = nullptr;
			continue;
		}
		if (count < kMaxNumPatchCables) {
			deluge::memory::shrink(destinations[g], sizeof(deluge::synth::RoutedDestination) * (count + 1));
		}
		std::copy_n(build.destinations[g].begin(), count + 1, destinations[g]);
	}

	// The cables have moved, so which are automated or interpolating is found again.
	summary.resetAutomationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	summary.resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		if (patchCables[c].param.isAutomated(*docForParam(c))) {
			flagCable(summary.whichParamsAreAutomated, c);
			if (patchCables[c].param.hasInterpolationIncrement()) {
				flagCable(summary.whichParamsAreInterpolating, c);
			}
		}
	}
}

// Searches unusable ones too
bool PatchCableSet::doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		if (patchCables[c].destinationParamDescriptor == destinationParamDescriptor) {
			return true;
		}
	}
	return false;
}

bool PatchCableSet::isSourcePatchedToDestinationDescriptorVolumeInspecific(PatchSource s,
                                                                           ParamDescriptor destinationParamDescriptor) {
	if (destinationParamDescriptor.getJustTheParam() == params::GLOBAL_VOLUME_POST_FX) {

		if (getPatchCableIndex(s, destinationParamDescriptor) != 255) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);

		if (getPatchCableIndex(s, destinationParamDescriptor) != 255) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::LOCAL_VOLUME);

		return (getPatchCableIndex(s, destinationParamDescriptor) != 255);
	}
	else {
		return (getPatchCableIndex(s, destinationParamDescriptor) != 255);
	}
}

bool PatchCableSet::isAnySourcePatchedToParamVolumeInspecific(ParamDescriptor destinationParamDescriptor) {

	if (destinationParamDescriptor.getJustTheParam() == params::GLOBAL_VOLUME_POST_FX) {

		if (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor)) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::GLOBAL_VOLUME_POST_REVERB_SEND);

		if (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor)) {
			return true;
		}

		destinationParamDescriptor.changeParam(params::LOCAL_VOLUME);

		return (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor));
	}
	else {
		return (doesDestinationDescriptorHaveAnyCables(destinationParamDescriptor));
	}
}

uint8_t PatchCableSet::getPatchCableIndex(PatchSource from, ParamDescriptor destinationParamDescriptor) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		if (patchCables[c].from == from && patchCables[c].destinationParamDescriptor == destinationParamDescriptor) {
			return c;
		}
	}
	return 255;
}

void PatchCableSet::deletePatchCable(Sound& sound, ParamManager& paramManager, uint8_t c) {
	if (c >= numPatchCables) {
		return;
	}
	// Its automation goes with it, and there is nothing to record: the cable is gone.
	eraseLane(patchCables[c].from, patchCables[c].destinationParamDescriptor);
	records_.set_target(c, {});
	patchCables[c].destinationParamDescriptor.setToNull();
	setupPatching(sound, paramManager);

	// The cable with no target is now in the unusable end of the list, where the render doesn't reach: move the last
	// cable into its place.
	for (int32_t i = numUsablePatchCables; i < numPatchCables - 1; i++) {
		if (patchCables[i].destinationParamDescriptor.isNull()) {
			patchCables[i].~PatchCable();
			memcpy(static_cast<void*>(&patchCables[i]), &patchCables[numPatchCables - 1], sizeof(PatchCable));
			records_.erase_by_moving_last(i);
			break;
		}
	}
	truncateCables(numPatchCables - 1);
}

void PatchCableSet::removeAllPatchingToParam(ModelStackWithParamCollection* modelStack, uint8_t p) {
	for (int32_t c = 0; c < numPatchCables; c++) {
		// The cables to the depths of p's cables go too.
		if (patchCables[c].destinationParamDescriptor.getJustTheParam() == p) {
			deletePatchCable(*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager, c);
		}
	}
}

#define FOR_EACH_FLAGGED_PARAM(whichParams)                                                                            \
	for (int32_t i = kNumUnsignedIntegersToRepPatchCables - 1; i >= 0; i--) {                                          \
		uint32_t whichParamsHere = whichParams[i];                                                                     \
		while (whichParamsHere) {                                                                                      \
			int32_t whichBit = 31 - clz(whichParamsHere);                                                              \
			whichParamsHere &= ~((uint32_t)1 << whichBit);                                                             \
			int32_t c = whichBit + (i << 5);

#define FOR_EACH_PARAM_END                                                                                             \
	}                                                                                                                  \
	}

void PatchCableSet::tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)

	AutoParam* param = &patchCables[c].param;
	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);

	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

	int32_t oldValue = param->getCurrentValue();
	bool shouldNotify = param->tickSamples(numSamples);
	if (shouldNotify) { // Should always actually be true...
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void PatchCableSet::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	AutoParam* param = &patchCables[c].param;
	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

	int32_t oldValue = param->getCurrentValue();
	param->setPlayPos(pos, modelStackWithAutoParam, reversed);

	FOR_EACH_PARAM_END

	ParamCollection::setPlayPos(pos, modelStack, reversed);
}

void PatchCableSet::playbackHasEnded(ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)

	patchCables[c].param.resetInterpolationIncrement();

	FOR_EACH_PARAM_END

	modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
}

void PatchCableSet::grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	AutoParam* param = &patchCables[c].param;
	int32_t oldValue = param->getCurrentValue();
	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);
	bool shouldNotify = param->grabValueFromPos(pos, modelStackWithAutoParam);
	if (shouldNotify) {
		notifyParamModifiedInSomeWay(modelStackWithAutoParam, oldValue, false, true, true);
	}

	FOR_EACH_PARAM_END
}

void PatchCableSet::generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
                                    bool shouldPingpong) {

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	patchCables[c].param.generateRepeats(*docForParam(c), oldLength, newLength, shouldPingpong);

	FOR_EACH_PARAM_END
}

void PatchCableSet::appendParamCollection(ModelStackWithParamCollection* modelStack,
                                          ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
                                          int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
	PatchCableSet* otherPatchCableSet = (PatchCableSet*)otherModelStack->paramCollection;

	FOR_EACH_FLAGGED_PARAM(
	    otherModelStack->summary->whichParamsAreAutomated) // Iterate through the *other* ParamManager's stuff

	PatchSource s = otherPatchCableSet->patchCables[c].from;
	int32_t i = getPatchCableIndex(s, otherPatchCableSet->patchCables[c].destinationParamDescriptor);

	if (i != 255) {
		patchCables[i].param.appendParam(*docForParam(i), *otherPatchCableSet->docForParam(c), oldLength,
		                                 reverseThisRepeatWithLength, pingpongingGenerally);
	}

	FOR_EACH_PARAM_END

	ticksTilNextEvent = 0;
}

void PatchCableSet::trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
                                 bool maySetupPatching) {

	bool anyStoppedBeingAutomated = false;

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
	AutoParam* param = &patchCables[c].param;

	ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);
	param->trimToLength(newLength, action, modelStackWithAutoParam);

	if (!param->hasInterpolationIncrement()) {
		unflagCable(modelStack->summary->whichParamsAreInterpolating, c);

		bool stillAutomated = param->isAutomated(*docForParam(c));
		if (!stillAutomated) {
			anyStoppedBeingAutomated = true;
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
	}

	FOR_EACH_PARAM_END

	if (maySetupPatching && anyStoppedBeingAutomated) {
		// Some cables stopped being automated: refresh which are.
		setupPatching(*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager);
	}

	ticksTilNextEvent = 0;
}

void PatchCableSet::shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
                                      int32_t effectiveLength) {
	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)
	patchCables[c].param.shiftHorizontally(*docForParam(c), amount, effectiveLength);
	FOR_EACH_PARAM_END
}

// Might return a ModelStack with NULL autoParam - check for that!
ModelStackWithAutoParam* PatchCableSet::getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation) {
	PatchSource s;
	ParamDescriptor paramDescriptor;
	dissectParamId(modelStack->paramId, &paramDescriptor, &s);
	uint8_t index = getPatchCableIndex(s, paramDescriptor);
	if (index == 255 && allowCreation) {
		// A write to a missing cable's depth adds the cable first, through the mod matrix, recorded in the open Action
		// so one undo takes back the write and the cable together.
		ModMatrixHost matrix{*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager};
		if (std::optional<CableEdit> added = matrix.ensureCable(s, deluge::synth::CableTarget{paramDescriptor.data})) {
			deluge::edits::recordCableEdit(modelStack, std::move(*added));
			index = getPatchCableIndex(s, paramDescriptor);
		}
	}
	return modelStack->addAutoParam(index == 255 ? nullptr : &patchCables[index].param);
}

void PatchCableSet::processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
                                      bool didPingpong, bool mayInterpolate) {

	ticksTilNextEvent -= ticksSkipped;

	if (ticksTilNextEvent <= 0) {

		modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables
		                                              - 1); // We'll repopulate this.

		ticksTilNextEvent = 2147483647;

		FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)
		int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
		AutoParam* param = &patchCables[c].param;
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStack->addAutoParam(paramId, param);

		int32_t ticksTilNextEventThisCable = param->processCurrentPos(modelStackWithAutoParam, reversed, didPingpong);
		ticksTilNextEvent = std::min(ticksTilNextEvent, ticksTilNextEventThisCable);

		if (param->hasInterpolationIncrement()) {
			flagCable(modelStack->summary->whichParamsAreInterpolating, c);
		}
		FOR_EACH_PARAM_END
	}
}

deluge::synth::RoutedDestination* PatchCableSet::cloneDestinations(deluge::synth::RoutedDestination const* src) {
	if (src == nullptr) {
		return nullptr;
	}
	// Sized to the list, including its terminator
	int32_t numEntries = 1;
	while (src[numEntries - 1].sources) {
		numEntries++;
	}
	auto* dst = static_cast<deluge::synth::RoutedDestination*>(
	    deluge::memory::alloc_fast(sizeof(deluge::synth::RoutedDestination) * numEntries));
	if (dst == nullptr) {
		throw deluge::exception::BAD_ALLOC;
	}
	deluge::synth::RoutedDestination* to = dst;
	deluge::synth::RoutedDestination const* from = src;
	while (true) {
		*to = *from; // trivially copyable
		if (!from->sources) {
			break; // copied the terminating "0" entry
		}
		from++;
		to++;
	}
	return dst;
}

PatchCableSet::PatchCableSet(PatchCableSet const& other)
    : ParamCollection(other), numUsablePatchCables(other.numUsablePatchCables), numPatchCables(other.numPatchCables) {
	sourcesPatchedToAnything[GLOBALITY_LOCAL] = other.sourcesPatchedToAnything[GLOBALITY_LOCAL];
	sourcesPatchedToAnything[GLOBALITY_GLOBAL] = other.sourcesPatchedToAnything[GLOBALITY_GLOBAL];

	// This set's own cable storage, sized to fit. Nothing renders a set under construction, so no exclusion.
	if (numPatchCables != 0) {
		patchCables = static_cast<PatchCable*>(
		    deluge::memory::alloc_fast(sizeof(PatchCable) * numPatchCables, alignof(PatchCable)));
		if (patchCables == nullptr) {
			throw deluge::exception::BAD_ALLOC;
		}
	}

	// Real-copy the live cables (deep-copies each cable's AutoParam nodes — may throw); the rest start empty, keeping
	// no automation. A throw destroys what was built, as the destructor won't run for a constructor that throws.
	int32_t built = 0;
	try {
		for (; built < numUsablePatchCables; built++) {
			new (&patchCables[built]) PatchCable(other.patchCables[built]);
		}
		// The clone owns a copy of its live cables' lanes, whichever table `other` reads them from; the rest of its
		// cables start empty, with no lane.
		for (int32_t c = 0; c < numUsablePatchCables; c++) {
			const deluge::plugin::ParamId id =
			    deluge::bridge::cable_depth(patchCables[c].from, patchCables[c].destinationParamDescriptor);
			for (const deluge::project::DocCableDepth& lane : other.depthStore()) {
				if (lane.depth == id) {
					ownDepths_.push_back(lane);
				}
			}
		}
		deluge::project::DocAutoParam empty{};
		for (; built < numPatchCables; built++) {
			new (&patchCables[built]) PatchCable();
			patchCables[built].param.init(empty);
		}

		// The records are the cables as built: the usable ones copied, the rest empty.
		for (int32_t c = 0; c < numPatchCables; c++) {
			records_.append(recordOf(patchCables[c]));
		}

		// Deep-copy the destination arrays last, so a throw leaves only the cables to clean up.
		destinations[GLOBALITY_LOCAL] = cloneDestinations(other.destinations[GLOBALITY_LOCAL]);
		try {
			destinations[GLOBALITY_GLOBAL] = cloneDestinations(other.destinations[GLOBALITY_GLOBAL]);
			if (other.routedCables_ != nullptr && numPatchCables != 0) {
				routedCables_ = static_cast<deluge::synth::RoutedCable*>(
				    deluge::memory::alloc_fast(sizeof(deluge::synth::RoutedCable) * numPatchCables));
				if (routedCables_ == nullptr) {
					throw deluge::exception::BAD_ALLOC;
				}
				std::copy_n(other.routedCables_, numPatchCables, routedCables_);
			}
			if (other.program_ != nullptr) {
				void* block =
				    deluge::memory::alloc_fast(other.program_->bytes, alignof(deluge::synth::matrix::MatrixProgram));
				if (block == nullptr) {
					throw deluge::exception::BAD_ALLOC;
				}
				program_ = deluge::synth::matrix::copy_program(*other.program_,
				                                               {static_cast<std::byte*>(block), other.program_->bytes});
			}
		} catch (deluge::exception&) {
			if (routedCables_ != nullptr) {
				delugeDealloc(routedCables_);
			}
			if (destinations[GLOBALITY_LOCAL]) {
				delugeDealloc(destinations[GLOBALITY_LOCAL]);
			}
			if (destinations[GLOBALITY_GLOBAL]) {
				delugeDealloc(destinations[GLOBALITY_GLOBAL]);
			}
			throw;
		}
	} catch (deluge::exception&) {
		for (int32_t c = 0; c < built; c++) {
			patchCables[c].~PatchCable();
		}
		deluge::memory::dealloc(patchCables);
		throw;
	}
}

Error PatchCableSet::clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength) const {
	void* mem = deluge::memory::alloc_fast(sizeof(PatchCableSet));
	if (mem == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	PatchCableSet* clone;
	try {
		clone = new (mem) PatchCableSet(*this);
	} catch (deluge::exception&) {
		delugeDealloc(mem);
		return Error::INSUFFICIENT_RAM;
	}
	if (!copyAutomation) {
		clone->stripAutomation();
	}
	else if (reverseDirectionWithLength != 0) {
		try {
			clone->flattenReverseAutomation(reverseDirectionWithLength);
		} catch (deluge::exception&) {
			clone->~PatchCableSet();
			delugeDealloc(mem);
			return Error::INSUFFICIENT_RAM;
		}
	}
	result = clone;
	return Error::NONE;
}

void PatchCableSet::stripAutomation() {
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		docForParam(c)->nodes.clear();
	}
}

void PatchCableSet::flattenReverseAutomation(int32_t reverseDirectionWithLength) {
	for (int32_t c = 0; c < numUsablePatchCables; c++) {
		patchCables[c].param.flattenReverse(*docForParam(c), reverseDirectionWithLength);
	}
}

void PatchCableSet::writePatchCablesToFile(Serializer& writer, bool writeAutomation) {
	if (!numPatchCables) {
		return;
	}

	// Patch cables
	writer.writeArrayStart("patchCables");
	for (int32_t c = 0; c < numPatchCables;
	     c++) { // I have a feeling this should actually only do up to "numUsablePatchCables"... otherwise we end up
		        // with like FM-related cables written to file for a subtractive preset... etc.
		if (!patchCables[c].destinationParamDescriptor.isJustAParam()) {
			continue; // If it's a depth-controlling cable, we'll deal with that separately, below.
		}

		writer.writeOpeningTagBeginning("patchCable", true);
		writer.writeAttribute("source", sourceToString(patchCables[c].from));
		writer.writeAttribute("destination",
		                      params::paramNameForFile(params::Kind::UNPATCHED_SOUND,
		                                               patchCables[c].destinationParamDescriptor.getJustTheParam()));
		writer.writeAttribute("polarity", polarityToString(patchCables[c].polarity).data());
		writer.insertCommaIfNeeded();
		writer.write("\n");
		writer.printIndents();
		writer.writeTagNameAndSeperator("amount");
		writer.write("\"");
		patchCables[c].param.writeToFile(*docForParam(c), writer, writeAutomation);
		writer.write("\"");

		// See if another cable(s) controls the range/depth of this cable
		ParamDescriptor paramDescriptor = patchCables[c].destinationParamDescriptor;
		paramDescriptor.addSource(patchCables[c].from);
		bool anyDepthControllingCablesFound = false;
		for (int32_t d = 0; d < numPatchCables; d++) {
			if (patchCables[d].destinationParamDescriptor == paramDescriptor) {
				if (!anyDepthControllingCablesFound) {
					anyDepthControllingCablesFound = true;
					writer.writeOpeningTagEnd();
					writer.writeArrayStart("depthControlledBy");
				}

				writer.writeOpeningTagBeginning("patchCable", true);
				writer.writeAttribute("source", sourceToString(patchCables[d].from));
				writer.writeAttribute("polarity", polarityToString(patchCables[d].polarity).data());
				writer.insertCommaIfNeeded();
				writer.write("\n");
				writer.printIndents();
				writer.writeTagNameAndSeperator("amount");
				writer.write("\"");
				patchCables[d].param.writeToFile(*docForParam(d), writer, writeAutomation);
				writer.write("\"");
				writer.closeTag(true);
			}
		}

		if (anyDepthControllingCablesFound) {
			writer.writeArrayEnding("depthControlledBy");
			writer.writeClosingTag("patchCable", true, true);
		}
		else {
			writer.closeTag(true);
		}
	}
	writer.writeArrayEnding("patchCables");
}

int32_t PatchCableSet::getParamId(ParamDescriptor destinationParamDescriptor, PatchSource s) {
	destinationParamDescriptor.addSource(s);
	return destinationParamDescriptor.data;
}

void PatchCableSet::dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, PatchSource* s) {
	ParamDescriptor paramDescriptor;
	paramDescriptor.data = paramId;

	*destinationParamDescriptor = paramDescriptor.getDestination();
	*s = paramDescriptor.getBottomLevelSource();
}

// Watch the heck out! This might delete the PatchCable, and AutoParam, in question.
void PatchCableSet::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                 bool automationChanged, bool automatedBefore, bool automatedNow) {

	ParamCollection::notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                              automatedNow);

	if (!modelStack->timelineCounterIsSet() || ((Clip*)modelStack->getTimelineCounter())->isActiveOnOutput()) {
		int32_t currentValue = modelStack->autoParam->getCurrentValue();
		bool currentValueChanged = (oldValue != currentValue);

		PatchSource s;
		ParamDescriptor destinationParamDescriptor;
		dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);

		if (currentValueChanged) {
			int32_t p =
			    destinationParamDescriptor
			        .getJustTheParam(); // Yes also do it if we've altered the "range" of a cable to p.... Although,
			                            // would the call below actually cause all of that to get recalculated?
			((Sound*)modelStack->modControllable)
			    ->recalculatePatchingToParam(p, (ParamManagerForTimeline*)modelStack->paramManager);
		}
	}

	if (automatedBefore != automatedNow) {
		PatchSource s;
		ParamDescriptor destinationParamDescriptor;
		dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);
		int32_t c = getPatchCableIndex(s, destinationParamDescriptor);

		if (automatedNow) {
			flagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
		else {
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
			unflagCable(modelStack->summary->whichParamsAreInterpolating, c);
		}
	}

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true; // Surely this could be more targeted?
}

void PatchCableSet::remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) {
	PatchSource s;
	ParamDescriptor destinationParamDescriptor;
	dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);

	int32_t c = getPatchCableIndex(s, destinationParamDescriptor);
	if (c == 255) {
		return;
	}
	AutoParam* param = &patchCables[c].param;

	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(param);

	param->swapState(state, modelStackWithParam);
}

void PatchCableSet::deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) {
	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
	AutoParam* autoParam = &patchCables[c].param;
	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(paramId, autoParam);
	autoParam->deleteAutomation(action, modelStackWithParam, false);

	FOR_EACH_PARAM_END

	modelStack->summary->resetAutomationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
	modelStack->summary->resetInterpolationRecord(kNumUnsignedIntegersToRepPatchCables - 1);
}

void PatchCableSet::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                    Action* action, ModelStackWithParamCollection* modelStack) {

	bool anyStoppedBeingAutomated = false;

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreAutomated)

	int32_t paramId = getParamId(patchCables[c].destinationParamDescriptor, patchCables[c].from);
	AutoParam* param = &patchCables[c].param;
	ModelStackWithAutoParam* modelStackWithParam = modelStack->addAutoParam(paramId, param);

	param->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action, modelStackWithParam);

	if (!param->hasInterpolationIncrement()) {
		unflagCable(modelStack->summary->whichParamsAreInterpolating, c);

		bool stillAutomated = param->isAutomated(*docForParam(c));
		if (!stillAutomated) {
			anyStoppedBeingAutomated = true;
			unflagCable(modelStack->summary->whichParamsAreAutomated, c);
		}
	}

	FOR_EACH_PARAM_END

	if (anyStoppedBeingAutomated) {
		// Some cables stopped being automated: refresh which are.
		setupPatching(*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager);
	}
}

int32_t PatchCableSet::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	return (paramValue >> 23) - 64;
}

int32_t PatchCableSet::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	int32_t paramValue = 1073741824;
	if (knobPos < 64) {
		paramValue = (knobPos + 64) << 23;
	}
	return paramValue;
}

void PatchCableSet::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {

	ParamCollection::notifyPingpongOccurred(modelStack);

	FOR_EACH_FLAGGED_PARAM(modelStack->summary->whichParamsAreInterpolating)
	patchCables[c].param.notifyPingpongOccurred();
	FOR_EACH_PARAM_END
}

void PatchCableSet::grabVelocityToLevelFromMIDIInput(LearnedMIDI* midiInput) {
	if (midiInput->containsSomething() && midiInput->cable) {
		MIDICable* cable = midiInput->cable;
		if (cable->hasDefaultVelocityToLevelSet()) {

			grabVelocityToLevelFromMIDICable(*cable);
		}
	}
}

void PatchCableSet::grabVelocityToLevelFromMIDICable(MIDICable& cable) {

	PatchCable* patchCable = getPatchCableFromVelocityToLevel();
	if (!patchCable) {
		return;
	}

	const uint8_t c = static_cast<uint8_t>(patchCable - patchCables);
	patchCable->param.setCurrentValueBasicForSetup(*docForParam(c), cable.defaultVelocityToLevel);
}

PatchCable* PatchCableSet::getPatchCableFromVelocityToLevel() {
	ParamDescriptor paramDescriptor;
	paramDescriptor.setToHaveParamOnly(params::LOCAL_VOLUME);

	// A new cable is placed by the setupPatching() the MIDI learn's callers run after it.
	int32_t i = getPatchCableIndex(PatchSource::VELOCITY, paramDescriptor);
	if (i == 255 && appendDefaultCable(PatchSource::VELOCITY, params::LOCAL_VOLUME, 0) != nullptr) {
		i = numPatchCables - 1;
	}
	if (i == 255) {
		return nullptr;
	}
	return &patchCables[i];
}
