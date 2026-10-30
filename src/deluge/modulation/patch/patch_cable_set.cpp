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
#include "deluge-project/param_automation.h"
#include "gui/views/view.h"
#include "io/midi/midi_device.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "modulation/patch/patch_cable.h"
#include "playback/mode/playback_mode.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/exceptions.h"
#include "util/misc.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <bitset>
#include <libdeluge/system.h>
#include <new>
#include <string.h>

namespace params = deluge::modulation::params;

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
		if (storage == nullptr) {
			return false;
		}
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
	// Shrinking only frees memory, so it can't fail for want of it: keep the cables where they are if it does.
	(void)resizeCables(std::min(count, numPatchCables));
	if (count < records_.size()) {
		records_.truncate(count);
	}
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
	cable->initAmount(0);
	return cable;
}

PatchCable* PatchCableSet::appendDefaultCable(PatchSource from, uint8_t to, int32_t amount) {
	PatchCable* cable = appendCable({from, deluge::synth::CableTarget::param(to),
	                                 static_cast<deluge::synth::CablePolarity>(PatchCable::getDefaultPolarity(from))});
	if (cable != nullptr) {
		cable->initAmount(amount);
	}
	return cable;
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

// Cables are reordered by index at runtime: setupPatching() memcpy-swaps them into usable/unusable
// order and removal memcpy-moves the last cable over a deleted one. An owner store resolved by INDEX cannot stay
// in sync with those shifts, so each cable's nodes live inline in its amount_, which the byte-copy
// carries with the cable. Binding to a shared positional store is a no-op; owner-resolution returns the inline store.
void PatchCableSet::bindDomStore() {
}

deluge::project::DocParamSet* PatchCableSet::domStore() {
	return nullptr; // never bound to a shared store (see bindDomStore)
}

deluge::project::DocAutoParam* PatchCableSet::docForParam(int32_t c) {
	// When bound to the owner's project-tree cable store, resolve this cable's amount-nodes from the tree
	// entry with its depth id (derived from its source and destination), so save + runtime edits share the
	// persistent store (the inline `amount_` is then unused). Unbound: the cable owns its store inline and rides
	// with it through the reorder/removal memcpy.
	if (domCablesRef_ != nullptr) {
		PatchCable& cable = patchCables[c];
		return &findOrCreateDomCable(cable.from, cable.destinationParamDescriptor, cable.polarity).amount;
	}
	return &patchCables[c].amount_;
}

deluge::project::DocCable& PatchCableSet::findOrCreateDomCable(PatchSource from, ParamDescriptor dest,
                                                               Polarity polarity) {
	const deluge::plugin::ParamId depth = deluge::bridge::cable_depth(from, dest);
	for (deluge::project::DocCable& entry : *domCablesRef_) {
		if (entry.depth == depth) {
			return entry;
		}
	}
	domCablesRef_->push_back(deluge::bridge::doc_cable(from, dest, polarity, {}));
	return domCablesRef_->back();
}

deluge::project::DocAutoParam* PatchCableSet::findDomCableAmount(PatchSource from, ParamDescriptor dest) const {
	if (domCablesRef_ == nullptr) {
		return nullptr;
	}
	const deluge::plugin::ParamId depth = deluge::bridge::cable_depth(from, dest);
	for (deluge::project::DocCable& entry : *domCablesRef_) {
		if (entry.depth == depth) {
			return &entry.amount;
		}
	}
	return nullptr;
}

deluge::project::DocAutoParam* PatchCableSet::docParamForId(int32_t paramId) {
	PatchSource s;
	ParamDescriptor desc;
	dissectParamId(paramId, &desc, &s);
	uint8_t c = getPatchCableIndex(s, desc);
	return (c == 255) ? nullptr : docForParam(c);
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

Destination* PatchCableSet::getDestinationForParam(int32_t p) {
	int32_t globality = (p < params::FIRST_GLOBAL) ? GLOBALITY_LOCAL : GLOBALITY_GLOBAL;

	// Special case - no Destinations at all
	if (!destinations[globality]) {
		return nullptr;
	}

	ParamDescriptor destinationParamDescriptor;
	destinationParamDescriptor.setToHaveParamOnly(p);

	// TODO: don't just linear search. But then, there won't be more than like 4 Destinations usually....
	for (Destination* destination = &destinations[globality][0]; destination->sources; destination++) {
		if (destination->destinationParamDescriptor == destinationParamDescriptor) {
			return destination;
		}
	}
	return nullptr;
}

const int32_t neutralRangeAdjustmentValue = 536870912;
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
		destinations[g] = (Destination*)deluge::memory::alloc_fast(sizeof(Destination) * (kMaxNumPatchCables + 1));
		if (!destinations[g]) {
			if (g == 1) {
				delugeDealloc(destinations[0]);
				destinations[0] = nullptr;
			}
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
	permuteCables(std::span{build.order}.first(build.num_cables));
	numUsablePatchCables = build.num_usable;

	for (int32_t c = 0; c < numPatchCables; c++) {
		const int8_t r = build.cables[c].range_index;
		patchCables[c].rangeAdjustmentPointer = r < 0 ? &neutralRangeAdjustmentValue : &rangeFinalValues[r];
	}
	for (int32_t g = 0; g < 2; g++) {
		sourcesPatchedToAnything[g] = build.sources_patched[g];
		const int32_t count = build.num_destinations[g];
		if (count == 0) {
			delugeDealloc(destinations[g]);
			destinations[g] = nullptr;
			continue;
		}
		if (count < kMaxNumPatchCables) {
			deluge::memory::shrink(destinations[g], sizeof(Destination) * (count + 1));
		}
		for (int32_t k = 0; k <= count; k++) {
			const deluge::synth::RoutedDestination& r = build.destinations[g][k];
			destinations[g][k].destinationParamDescriptor = ParamDescriptor(static_cast<int32_t>(r.target.word));
			destinations[g][k].sources = r.sources;
			destinations[g][k].firstCable = r.first_cable;
			destinations[g][k].endCable = r.end_cable;
		}
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

uint8_t PatchCableSet::findOrCreateCable(Sound& sound, ParamManager& paramManager, PatchSource from,
                                         ParamDescriptor destination) {
	if (const uint8_t c = getPatchCableIndex(from, destination); c != 255) {
		return c;
	}
	if (appendCable(
	        {from, {destination.data}, static_cast<deluge::synth::CablePolarity>(PatchCable::getDefaultPolarity(from))})
	    == nullptr) {
		return 255;
	}
	// The compile places the new cable, moving others, so it is found again.
	setupPatching(sound, paramManager);
	return getPatchCableIndex(from, destination);
}

void PatchCableSet::deletePatchCable(Sound& sound, ParamManager& paramManager, uint8_t c) {
	if (c >= numPatchCables) {
		return;
	}
	// Its automation goes with it, and there is nothing to record: the cable is gone.
	patchCables[c].param.deleteAutomationBasicForSetup(*docForParam(c));
	records_.set_target(c, {});
	patchCables[c].destinationParamDescriptor.setToNull();
	setupPatching(sound, paramManager);

	// The cable with no target is now in the unusable end of the list, where the render doesn't reach: move the last
	// cable into its place.
	for (int32_t i = numUsablePatchCables; i < numPatchCables - 1; i++) {
		if (patchCables[i].destinationParamDescriptor.isNull()) {
			patchCables[i].~PatchCable();
			memcpy(static_cast<void*>(&patchCables[i]), &patchCables[numPatchCables - 1], sizeof(PatchCable));
			// The moved cable's automation now belongs to slot i: the old slot must not free it too.
			patchCables[numPatchCables - 1].amount_.nodes.init();
			records_.erase_by_moving_last(i);
			break;
		}
	}
	truncateCables(numPatchCables - 1);
}

// PatchCableSet::getModifiedPatchCableAmount is defined in patch_cable_set_amount.cpp (a gui-free TU, so the
// standalone synth core can link it).

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
		// In case the absence of automation here, presumably in conjunction with a value of 0, means this Cable is now
		// inconsequential and can be deleted.
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
		index = findOrCreateCable(*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager, s,
		                          paramDescriptor);
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

Destination* PatchCableSet::cloneDestinations(Destination const* src) {
	if (src == nullptr) {
		return nullptr;
	}
	// Sized to the list, including its terminator
	int32_t numEntries = 1;
	while (src[numEntries - 1].sources) {
		numEntries++;
	}
	auto* dst = static_cast<Destination*>(deluge::memory::alloc_fast(sizeof(Destination) * numEntries));
	if (dst == nullptr) {
		throw deluge::exception::BAD_ALLOC;
	}
	Destination* to = dst;
	Destination const* from = src;
	while (true) {
		*to = *from; // Destination is trivially copyable.
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
			PatchCable* cable = new (&patchCables[built]) PatchCable(other.patchCables[built]);
			// If `other` reads its live nodes from an external tree store, its inline amount_ is stale (edits
			// went to the tree). Copy the current tree nodes so this clone starts self-owned (domCablesRef_ stays
			// null) with correct automation — mirroring ParamSet's copy-ctor deep-copying its bound store.
			if (deluge::project::DocAutoParam* live =
			        other.findDomCableAmount(cable->from, cable->destinationParamDescriptor)) {
				cable->amount_ = *live;
			}
		}
		for (; built < numPatchCables; built++) {
			new (&patchCables[built]) PatchCable();
			patchCables[built].param.init(*docForParam(built));
		}

		// The records are the cables as built: the usable ones copied, the rest empty.
		for (int32_t c = 0; c < numPatchCables; c++) {
			records_.append(recordOf(patchCables[c]));
		}

		// Deep-copy the destination arrays last, so a throw leaves only the cables to clean up.
		destinations[GLOBALITY_LOCAL] = cloneDestinations(other.destinations[GLOBALITY_LOCAL]);
		try {
			destinations[GLOBALITY_GLOBAL] = cloneDestinations(other.destinations[GLOBALITY_GLOBAL]);
		} catch (deluge::exception&) {
			if (destinations[GLOBALITY_LOCAL]) {
				delugeDealloc(destinations[GLOBALITY_LOCAL]);
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

	bool haveRedoneSetup = false;

	if (!modelStack->timelineCounterIsSet() || ((Clip*)modelStack->getTimelineCounter())->isActiveOnOutput()) {
		int32_t currentValue = modelStack->autoParam->getCurrentValue();
		bool currentValueChanged = (oldValue != currentValue);

		PatchSource s;
		ParamDescriptor destinationParamDescriptor;
		dissectParamId(modelStack->paramId, &destinationParamDescriptor, &s);

		// Delete the patch cable if its value is now 0 and no automation. This is a bit "dangerous" - it will probably
		// delete the AutoParam that called us!
		if (!modelStack->autoParam->containsSomething(*modelStack->doc(), 0)) {
			int32_t c = getPatchCableIndex(s, destinationParamDescriptor);
			if (c != 255) {
				deletePatchCable(*static_cast<Sound*>(modelStack->modControllable), *modelStack->paramManager, c);
				haveRedoneSetup = true;
			}
		}

		if (currentValueChanged) {
			int32_t p =
			    destinationParamDescriptor
			        .getJustTheParam(); // Yes also do it if we've altered the "range" of a cable to p.... Although,
			                            // would the call below actually cause all of that to get recalculated?
			((Sound*)modelStack->modControllable)
			    ->recalculatePatchingToParam(p, (ParamManagerForTimeline*)modelStack->paramManager);
		}
	}

	if (!haveRedoneSetup && automatedBefore != automatedNow) {
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
		// In case the absence of automation here, presumably in conjunction with a value of 0, means this Cable is now
		// inconsequential and can be deleted.
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

	// Write the cable's inline store (amount_, the persisted source when unbound) too, not just the cursor.
	patchCable->param.setCurrentValueBasicForSetup(patchCable->amount_, cable.defaultVelocityToLevel);
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
