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

#include "modulation/params/param_manager.h"
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_param_change.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/params/tick_interpolation.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include <array>
#include <cstddef>
#include <new>

ParamManager::ParamManager() {
	summaries[0] = {0};
	summaries[1] = {0};
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
	expressionParamSetOffset = 0;

	resonanceBackwardsCompatibilityProcessed = false;
}

ParamManager::~ParamManager() {
	destructAndForgetParamCollections();
}

ParamManagerForTimeline* ParamManager::toForTimeline() {
	FREEZE_WITH_ERROR("E407");
	return nullptr;
}

ParamManagerForTimeline* ParamManagerForTimeline::toForTimeline() {
	return this;
}

namespace {
// Offsets of collections sharing an allocation.
constexpr uint32_t alignCollection(uint32_t size) {
	return (size + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
}
} // namespace

Error ParamManager::setupMIDI() {
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(MIDIParamCollection));
	if (!memory) {
		return Error::INSUFFICIENT_RAM;
	}

	destructAndForgetMainParamCollections(); // Don't leak a previous setup. Leaves any expression params in slot 0
	summaries[1] = summaries[0];             // Shuffle the expression params over.
	summaries[0].paramCollection = new (memory) MIDIParamCollection(&summaries[0]);
	summaries[2] = {0};
	expressionParamSetOffset = 1;
	return Error::NONE;
}

Error ParamManager::setupUnpatched() {
	void* memory_unpatched = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(UnpatchedParamSet));
	if (memory_unpatched == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	destructAndForgetMainParamCollections(); // Don't leak a previous setup. Leaves any expression params in slot 0
	summaries[1] = summaries[0];             // Shuffle the expression params over, rather than overwriting them
	// ParamManager owns these placement-constructed collections and destroys them before deallocation.
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
	summaries[0].paramCollection = new (memory_unpatched) UnpatchedParamSet(&summaries[0]);
	summaries[2] = {0};
	expressionParamSetOffset = 1;
	return Error::NONE;
}

Error ParamManager::setupWithPatching() {
	// The main collections are always allocated, moved and freed together, so they share one
	// allocation: one block in the allocator's size classes rather than three.
	constexpr uint32_t patched_offset = alignCollection(sizeof(UnpatchedParamSet));
	constexpr uint32_t patch_cables_offset = patched_offset + alignCollection(sizeof(PatchedParamSet));
	auto* memory =
	    static_cast<char*>(GeneralMemoryAllocator::get().allocMaxSpeed(patch_cables_offset + sizeof(PatchCableSet)));
	if (memory == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	void* memory_unpatched = memory;
	void* memory_patched = memory + patched_offset;
	void* memory_patch_cables = memory + patch_cables_offset;

	destructAndForgetMainParamCollections(); // Don't leak a previous setup. Leaves any expression params in slot 0
	summaries[3] = summaries[0];             // Shuffle the expression params over, rather than overwriting them
	// ParamManager owns these placement-constructed collections and destroys them before deallocation.
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
	summaries[0].paramCollection = new (memory_unpatched) UnpatchedParamSet(&summaries[0]);
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
	summaries[1].paramCollection = new (memory_patched) PatchedParamSet(&summaries[1]);
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
	summaries[2].paramCollection = new (memory_patch_cables) PatchCableSet(&summaries[2]);
	summaries[4] = {0};
	expressionParamSetOffset = 3;
	return Error::NONE;
}

// Make sure other isn't NULL before you call this, you muppet.
void ParamManager::stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams) {
	if (!other) {
		FREEZE_WITH_ERROR("E413");
	}
	// don't steal from ourselves
	if (other == this) {
		return;
	}

	int32_t mpeParamsOffsetOther = other->getExpressionParamSetOffset();
	int32_t mpeParamsOffsetHere = getExpressionParamSetOffset();
	int32_t stopAtOther = mpeParamsOffsetOther;

	// Main collections already owned by the destination are about to be
	// overwritten. Release them while retaining its expression collection.
	releaseMainParamCollections();
	for (int32_t i = 0; i < mpeParamsOffsetHere; i++) {
		summaries[i] = ParamCollectionSummary{};
	}

	// If we're planning to steal expression params, and yes "other" does in fact have them...
	if (stealExpressionParams && other->summaries[stopAtOther].paramCollection) {

		// If "here" has them too, we'll just keep these, and destruct "other"'s ones
		if (summaries[mpeParamsOffsetHere].paramCollection) {
			other->summaries[stopAtOther].paramCollection->~ParamCollection();
			delugeDealloc(other->summaries[stopAtOther].paramCollection);
			other->summaries[stopAtOther] = {0};
		}

		// Otherwise, yup, proceed to steal them
		else {
			stopAtOther++;
		}
	}

	ParamCollectionSummary hereMpeParamsOrNull = summaries[mpeParamsOffsetHere];

	int32_t i;
	for (i = 0; i < stopAtOther; i++) {
		summaries[i] = other->summaries[i];
		other->summaries[i] = {0};
	}

	summaries[stopAtOther] = hereMpeParamsOrNull; // Could the expression params, or NULL
	if (hereMpeParamsOrNull.paramCollection) {
		summaries[stopAtOther + 1] = {0}; // If that was expression params, write the actual terminating NULL here
	}
	// - but not otherwise, cos we could have overflowed past the array's size!
	expressionParamSetOffset = mpeParamsOffsetOther;

	other->summaries[0] = other->summaries[stopAtOther];
	other->summaries[1] = {0};
	other->summaries[2] = {0};
	other->summaries[3] = {0};
	other->summaries[4] = {0};
	other->expressionParamSetOffset = 0;
}

Error ParamManager::cloneParamCollectionsFrom(ParamManager const* other, bool copyAutomation,
                                              bool cloneExpressionParams, int32_t reverseDirectionWithLength) {

	ParamCollectionSummary mpeParamsOrNullHere = *getExpressionParamSetSummary();
	// Paul: Prevent MPE data from not getting exchanged with a newly allocated pointer if we allocate the same params
	// for another clip
	if (this != other) {
		if (mpeParamsOrNullHere.paramCollection) {
			cloneExpressionParams = false; // If we already have expression params, then just don't clone from "other".
		}
	}

	// First, allocate the memories
	ParamCollectionSummary
	    newSummaries[PARAM_COLLECTIONS_STORAGE_NUM]; // Temporary separate storage, so we can clone from self (when this
	                                                 // function is called from beenCloned()).

	ParamCollectionSummary* __restrict__ newSummary = newSummaries;
	ParamCollectionSummary const* otherSummary =
	    other->summaries; // Not __restrict__, because other might be the same as this!
	ParamCollectionSummary const* otherStopAt = &other->summaries[other->expressionParamSetOffset];

	if (cloneExpressionParams && otherStopAt->paramCollection) {
		otherStopAt++;
	}

	// The main collections share one allocation, as when set up; expression params have their own.
	ParamCollectionSummary const* other_main_end = &other->summaries[other->expressionParamSetOffset];
	uint32_t main_size = 0;
	for (auto const* summary = otherSummary; summary != other_main_end; summary++) {
		main_size += alignCollection(summary->paramCollection->objectSize);
	}
	char* main_memory = nullptr;
	while (otherSummary != otherStopAt) {
		// To cut corners, we store this currently blank/undefined memory in our array of type ParamCollectionSummary
		if (otherSummary == other_main_end) {
			newSummary->paramCollection = static_cast<ParamCollection*>(
			    GeneralMemoryAllocator::get().allocMaxSpeed(otherSummary->paramCollection->objectSize));
		}
		else {
			if (main_memory == nullptr) {
				main_memory = static_cast<char*>(GeneralMemoryAllocator::get().allocMaxSpeed(main_size));
				main_size = 0;
			}
			newSummary->paramCollection = nullptr;
			if (main_memory != nullptr) {
				// The collection is constructed in its part of the shared block.
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
				newSummary->paramCollection = reinterpret_cast<ParamCollection*>(main_memory + main_size);
			}
			main_size += alignCollection(otherSummary->paramCollection->objectSize);
		}

		// If that failed, deallocate the previous memory
		if (!newSummary->paramCollection) {
			if (main_memory != nullptr) {
				delugeDealloc(main_memory);
			}

			if (this == other) {
				// beenCloned() runs on a shallow copy of a NoteRow, so none of these pointers - expression included -
				// belong to us yet. Keeping any of them would free the original's collections when we're destructed.
				for (auto& summary : summaries) {
					summary = {0};
				}
				expressionParamSetOffset = 0;
			}
			// Otherwise leave our existing collections untouched, rather than forgetting (and leaking) them
			return Error::INSUFFICIENT_RAM;
		}

		newSummary++;
		otherSummary++;
	}

	// Now the memories have been allocated, go through and do the cloning
	newSummary = newSummaries;
	otherSummary = other->summaries;
	while (otherSummary != otherStopAt) {

		memcpy(newSummary->paramCollection, otherSummary->paramCollection, otherSummary->paramCollection->objectSize);

		newSummary->paramCollection->beenCloned(
		    copyAutomation, reverseDirectionWithLength); // Ignore error - just means automation doesn't get cloned.

		newSummary->cloneFlagsFrom(otherSummary);

		newSummary++;
		otherSummary++;
	}

	// Paul: If we move allocation position of the same clip mpe data was allocated above and doesn't require special
	// treatment
	if (this == other) {
		*newSummary = {0}; // Mark end of list
	}
	else {
		*newSummary = mpeParamsOrNullHere;
		if (mpeParamsOrNullHere.paramCollection) { // Check first, otherwise we'll overflow the array, I think...
			newSummary++;
			*newSummary = {0}; // Mark end of list
		}
	}

	// Any main collections we already owned are about to be overwritten - free them (keeping our expression params,
	// which were captured above). When cloning from self, the pointers aren't ours to free.
	if (this != other) {
		destructAndForgetMainParamCollections();
	}

	// And finally, copy the pointers and flags from newSummaries to our permanent summaries array.
	newSummary = newSummaries;
	ParamCollectionSummary* destSummaries = summaries;
	while (true) {
		*destSummaries = *newSummary;
		if (!newSummary->paramCollection) {
			break;
		}
		destSummaries++;
		newSummary++;
	}
	// Clear anything left past the terminator from a previous, longer layout
	for (destSummaries++; destSummaries != &summaries[PARAM_COLLECTIONS_STORAGE_NUM]; destSummaries++) {
		*destSummaries = {0};
	}

	expressionParamSetOffset = other->expressionParamSetOffset;

	return Error::NONE;
}

// This is only called once - for NoteRows after cloning an InstrumentClip.
Error ParamManager::beenCloned(int32_t reverseDirectionWithLength) {
	return cloneParamCollectionsFrom(this, true, true, reverseDirectionWithLength); // *Does* clone expression params
}

// Does *not* forget MPE params
void ParamManager::forgetParamCollections() {
	// Copy the whole summary, not just the pointer - otherwise the expression set inherits the automated/interpolating
	// flags of whatever main collection was in slot 0, and FOR_EACH_FLAGGED_PARAM would index past its params
	summaries[0] = *getExpressionParamSetSummary();
	summaries[1] = {0};
	expressionParamSetOffset = 0;
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
}

// This one deletes MPE params too
void ParamManager::destructAndForgetParamCollections() {
	releaseMainParamCollections();
	ParamCollection* expression = summaries[expressionParamSetOffset].paramCollection;
	if (expression != nullptr) {
		expression->~ParamCollection();
		delugeDealloc(expression);
	}

	summaries[0] = {0};
	expressionParamSetOffset = 0;
}

// The main collections share the allocation that starts with the first of them.
void ParamManager::releaseMainParamCollections() {
	for (int32_t i = 0; i < expressionParamSetOffset; i++) {
		if (summaries[i].paramCollection) {
			summaries[i].paramCollection->~ParamCollection();
		}
	}
	if (expressionParamSetOffset > 0 && summaries[0].paramCollection != nullptr) {
		delugeDealloc(summaries[0].paramCollection);
	}
}

void ParamManager::destructAndForgetMainParamCollections() {
	releaseMainParamCollections();
	forgetParamCollections(); // Shuffles any expression params down to the start
}

// Returns whether there is one / one could be created.
bool ParamManager::ensureExpressionParamSetExists(bool forDrum) {
	int32_t offset = getExpressionParamSetOffset();
	if (!summaries[offset].paramCollection) {

		void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(ExpressionParamSet));
		if (!memory) {
			return false;
		}

		summaries[offset].paramCollection = new (memory) ExpressionParamSet(&summaries[offset], forDrum);
		summaries[offset + 1] = {0};
	}
	return true;
}

ExpressionParamSet* ParamManager::getOrCreateExpressionParamSet(bool forDrum) {
	if (!ensureExpressionParamSetExists(forDrum)) {
		return nullptr;
	}

	return getExpressionParamSet();
}

ModelStackWithParamCollection* ParamManager::getPatchCableSet(ModelStackWithThreeMainThings const* modelStack) {
	if (!summaries[2].paramCollection) {
		FREEZE_WITH_ERROR("E412");
	}
	return modelStack->addParamCollection(summaries[2].paramCollection, &summaries[2]);
}

ParamManagerForTimeline::ParamManagerForTimeline() {
	ticksSkipped = 0;
	ticksTilNextEvent = 0;
}

// Even if it's just expression params.
void ParamManagerForTimeline::ensureSomeParamCollections() {
	if (!summaries[0].paramCollection) {
		FREEZE_WITH_ERROR("E408");
	}
}

#define FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START                                                      \
	ParamCollectionSummary* summary = summaries;                                                                       \
	do {                                                                                                               \
		if (summary->containsAutomation()) {                                                                           \
			ModelStackWithParamCollection* modelStackWithParamCollection =                                             \
			    modelStack->addParamCollectionSummary(summary);

#define FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END                                                        \
	}                                                                                                                  \
	summary++;                                                                                                         \
	}                                                                                                                  \
	while (summary->paramCollection)                                                                                   \
		;

// ------------------------------------------------------------------------------------------------------------------------------------------------------

#define FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_START                                                               \
	ParamCollectionSummary* summary = summaries;                                                                       \
	while (summary->paramCollection) {                                                                                 \
		if (summary->containsAutomation()) {                                                                           \
			ModelStackWithParamCollection* modelStackWithParamCollection =                                             \
			    modelStack->addParamCollectionSummary(summary);

#define FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_END                                                                 \
	}                                                                                                                  \
	summary++;                                                                                                         \
	}

// You'll usually want to call mightContainAutomation() before bothering with this, to save time.
void ParamManagerForTimeline::processCurrentPos(ModelStackWithThreeMainThings* modelStack, int32_t ticksSinceLast,
                                                bool reversed, bool didPingpong, bool mayInterpolate) {

	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	ticksSkipped += ticksSinceLast;
	ticksTilNextEvent -= ticksSinceLast;

	if (ticksTilNextEvent <= 0) {

		ticksTilNextEvent = 2147483647;

		FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

		// If we can't interpolate by samples then we'll interpolate by ticks instead. This has to happen *before*
		// processCurrentPos(), because that may reach a node and set up a new increment for the span we're about to
		// begin - whereas the ticks we've skipped belong to the span we've just finished. Applying them to the new
		// increment sends the value far past its target (a whole inter-node gap's worth of a few-tick ramp), which for
		// MIDI output means expression values well outside 0-127.
		if (!mayInterpolate && (summary->whichParamsAreInterpolating[0] != 0u)) {
			summary->paramCollection->tickTicks(ticksSkipped, modelStackWithParamCollection);
		}

		summary->paramCollection->processCurrentPos(modelStackWithParamCollection, ticksSkipped, reversed, didPingpong,
		                                            true);
		// Re-check after processCurrentPos(): if a node has just started some interpolation, we need to come back every
		// tick (or every few at very fast tempos) to advance it. The min below still brings us back for the next node.
		if (!mayInterpolate && (summary->whichParamsAreInterpolating[0] != 0u)) {
			ticksTilNextEvent =
			    std::min(ticksTilNextEvent, ticksTilNextTickInterpolation(playbackHandler.getTimePerInternalTick()));
		}
		ticksTilNextEvent = std::min(ticksTilNextEvent, summary->paramCollection->ticksTilNextEvent);

		FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

		ticksSkipped = 0;
	}
}

void ParamManagerForTimeline::expectEvent(ModelStackWithThreeMainThings const* modelStack) {
	TimelineCounter* timelineCounter = modelStack->getTimelineCounterAllowNull();
	if (playbackHandler.isEitherClockActive() && (!timelineCounter || timelineCounter->isPlayingAutomationNow())) {
		ticksTilNextEvent = 0;
		if (timelineCounter) {
			timelineCounter->expectEvent();
		}
	}
}

// Very minimal function - doesn't take a ModelStack, because we use this to decide whether we even need to create /
// populate the ModelStack.
bool ParamManagerForTimeline::mightContainAutomation() {

	ParamCollectionSummary* summary = summaries;
	while (summary->paramCollection) {
		if (summary->containsAutomation()) {
			return true;
		}
		summary++;
	}

	return false;
}

// You'll usually want to call mightContainAutomation() before bothering with this, to save time.
void ParamManagerForTimeline::setPlayPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack, bool reversed) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->setPlayPos(pos, modelStackWithParamCollection, reversed);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	expectEvent(modelStack);
	ticksSkipped = 0;
}

void ParamManagerForTimeline::grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack) {

	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->grabValuesFromPos(pos, modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManager::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t currentValueChanged,
                                                bool automationChanged, bool paramAutomatedNow) {
	if (automationChanged && paramAutomatedNow) {
		toForTimeline()->expectEvent(modelStack);
	}

	if (currentValueChanged) {
		view.notifyParamAutomationOccurred(this);
	}
}

void ParamManagerForTimeline::shiftHorizontally(ModelStackWithThreeMainThings* modelStack, int32_t amount,
                                                int32_t effectiveLength) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, effectiveLength);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManagerForTimeline::deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->deleteAllAutomation(action, modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManagerForTimeline::trimToLength(uint32_t newLength, ModelStackWithThreeMainThings* modelStack,
                                           Action* action, bool maySetupPatching) {

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_START

	summary->paramCollection->trimToLength(newLength, modelStackWithParamCollection, action, maySetupPatching);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_END
}

void ParamManagerForTimeline::generateRepeats(ModelStackWithThreeMainThings* modelStack, uint32_t oldLength,
                                              uint32_t newLength, bool shouldPingpong) {

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_START

	summary->paramCollection->generateRepeats(modelStackWithParamCollection, oldLength, newLength, shouldPingpong);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_IF_ANY_END
}

void ParamManagerForTimeline::appendParamManager(ModelStackWithThreeMainThings* modelStack,
                                                 ModelStackWithThreeMainThings* otherModelStack, int32_t oldLength,
                                                 int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
	otherModelStack->paramManager->toForTimeline()
	    ->ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do"
	                                    // below with its "while".

	ParamCollectionSummary* otherSummary = otherModelStack->paramManager->summaries;
	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	ModelStackWithParamCollection* otherModelStackWithParamCollection =
	    otherModelStack->addParamCollectionSummary(otherSummary);
	summary->paramCollection->appendParamCollection(modelStackWithParamCollection, otherModelStackWithParamCollection,
	                                                oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
}
summary++;
otherSummary++;
}
while (summary->paramCollection)
	;

ticksTilNextEvent = 0; // Should probably really call expectEvent(), but we're only called when a tick is just about to
                       // happen anyway, so shouldn't matter
}

// Note: you must only call this if playbackHandler.isEitherClockActive()
void ParamManagerForTimeline::tickSamples(int32_t numSamples, ModelStackWithThreeMainThings* modelStack) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	// Beware - for efficiency, the caller of this sometimes pre-checks whether to even call this at all

	ParamCollectionSummary* summary = summaries;
	do {
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addParamCollection(summary->paramCollection, summary);
		summary->paramCollection->tickSamples(numSamples, modelStackWithParamCollection);
		summary++;
	} while (summary->paramCollection);
}

void ParamManagerForTimeline::nudgeAutomationHorizontallyAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop,
                                                               Action* action,
                                                               ModelStackWithThreeMainThings* modelStack,
                                                               bool nudgeAutomation, bool nudgeMPE,
                                                               int32_t moveMPEDataWithinRegionLength) {
	// the following code iterates through all param collections and nudges automation and MPE separately
	// automation only gets nudged if nudgeAutomation is true
	// MPE only gets nudged if nudgeMPE is true

	ParamCollectionSummary* summary = summaries;
	int32_t i = 0;
	while (summary->paramCollection) {
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addParamCollection(summary->paramCollection, summary);

		// Special case for MPE only - not even "mono" / Clip-level expression.
		if (moveMPEDataWithinRegionLength && i == getExpressionParamSetOffset()) {
			if (nudgeMPE) {
				((ExpressionParamSet*)summary->paramCollection)
				    ->moveRegionHorizontally(modelStackWithParamCollection, pos, moveMPEDataWithinRegionLength, offset,
				                             lengthBeforeLoop, action);
			}
		}

		// Normal case (non MPE automation)
		else {
			if (nudgeAutomation) {
				summary->paramCollection->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action,
				                                                          modelStackWithParamCollection);
			}
		}
		summary++;
		i++;
	}
}

void ParamManagerForTimeline::notifyPingpongOccurred(ModelStackWithThreeMainThings* modelStack) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START
	summary->paramCollection->notifyPingpongOccurred(modelStackWithParamCollection);
	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	ticksTilNextEvent = 0;
}

void ParamManagerForTimeline::expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack) {
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->playbackHasEnded(modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

/*
        ParamCollection** paramCollection = paramCollections;
        do {
            ModelStackWithParamCollection* modelStackWithParamCollection =
   modelStack->addParamCollection((*paramCollection));
            (*paramCollection)->
            paramCollection++;
        } while (*paramCollection);
 */

namespace {
// Announce a value the reset changed. An inactive MIDI clip bypasses MIDIParamCollection's notification, which
// would send its value to the output.
PLACE_SDRAM_TEXT void notifyResetValue(ParamCollection* collection, ModelStackWithAutoParam* stack, int32_t old_value,
                                       bool automated) {
	if (collection->getParamKind() == deluge::modulation::params::Kind::MIDI && stack->timelineCounterIsSet()
	    && !static_cast<Clip*>(stack->getTimelineCounter())->isActiveOnOutput()) {
		collection->ParamCollection::notifyParamValueModified(stack, old_value, stack->autoParam->getCurrentValue(),
		                                                      false, automated, automated);
	}
	else {
		collection->notifyParamModifiedInSomeWay(stack, old_value, false, automated, automated);
	}
}

// What a parameter's own snapshot can't restore: whether the reset created a patch route or MIDI CC, so undo
// deletes it again and redo creates it, or the polarity of a route the reset deleted. Undo recreates a deleted
// route with its default polarity from the snapshot, which is newer and so undone first.
class ConsequenceResetEntry final : public Consequence {
public:
	ConsequenceResetEntry(ModelStackWithParamId const* model_stack, bool created, Polarity polarity)
	    : created_(created), polarity_(polarity) {
		type = RESET_PARAM_ENTRY;
		memcpy(model_stack_memory_.data(), model_stack, sizeof(ModelStackWithParamId));
	}
	Error revert(TimeType time, ModelStack* /*modelStack*/) override {
		// The copied stack is used as ConsequenceParamChange uses its own.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		auto* stack = reinterpret_cast<ModelStackWithParamId*>(model_stack_memory_.data());
		auto* collection = stack->paramCollection;
		bool cable = collection->getParamKind() == deluge::modulation::params::Kind::PATCH_CABLE;
		if (!created_) {
			if (time == BEFORE && cable) {
				auto* cables = static_cast<PatchCableSet*>(collection);
				PatchSource source = PatchSource::NOT_AVAILABLE;
				ParamDescriptor destination;
				PatchCableSet::dissectParamId(stack->paramId, &destination, &source);
				uint8_t c = cables->getPatchCableIndex(source, destination);
				if (c != 255) {
					// Rendering reads the polarity directly, so no patching setup is needed.
					cables->patchCables[c].polarity = polarity_;
				}
			}
			return Error::NONE;
		}
		if (time == BEFORE) {
			if (cable) {
				static_cast<PatchCableSet*>(collection)->deleteRoute(stack, stack->paramId);
			}
			else {
				static_cast<MIDIParamCollection*>(collection)->deleteParam(stack->paramId);
			}
			return Error::NONE;
		}
		auto* param_stack = collection->getAutoParamFromId(stack, true);
		if (param_stack->autoParam == nullptr) {
			return Error::INSUFFICIENT_RAM;
		}
		notifyResetValue(collection, param_stack, 0, false);
		return Error::NONE;
	}

private:
	alignas(ModelStackWithParamId) std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory_{};
	bool created_;
	Polarity polarity_;
};

PLACE_SDRAM_TEXT Error recordResetEntry(Action& action, ModelStackWithParamId const* stack, bool created,
                                        Polarity polarity) {
	void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceResetEntry));
	if (memory == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	// Ownership passes to the action, which destroys and deallocates the consequence.
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
	action.addConsequence(new (memory) ConsequenceResetEntry(stack, created, polarity));
	return Error::NONE;
}

// Snapshot a parameter before the reset changes it. The snapshot must hold every automation node, or undo
// would delete the curve.
PLACE_SDRAM_TEXT Error snapshotResetValue(Action& action, ModelStackWithAutoParam* stack) {
	action.recordParamChangeIfNotAlreadySnapshotted(stack, false);
	for (auto* cons = action.firstConsequence; cons != nullptr; cons = cons->next) {
		if (cons->type != Consequence::PARAM_CHANGE) {
			continue;
		}
		auto* change = static_cast<ConsequenceParamChange*>(cons);
		// Only compare the recorded collection; never dereference it.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		const auto& recorded = change->modelStack;
		if (recorded.paramCollection == stack->paramCollection && recorded.paramId == stack->paramId) {
			return change->state.nodes.getNumElements() == stack->autoParam->nodeCount() ? Error::NONE
			                                                                             : Error::INSUFFICIENT_RAM;
		}
	}
	return Error::INSUFFICIENT_RAM;
}

PLACE_SDRAM_TEXT Error resetParameterValue(ParamCollection* collection, ParamCollectionSummary* summary,
                                           ModelStackWithThreeMainThings* model_stack, int32_t id, bool create,
                                           Action& action, bool apply) {
	auto* scalar = collection->getClipValueStorage(id);
	bool existed = scalar != nullptr;
	if ((scalar != nullptr && scalar->getOffset() == 0
	     && collection->getParamKind() != deluge::modulation::params::Kind::PATCH_CABLE)
	    || (scalar == nullptr && !create)) {
		return Error::NONE;
	}
	if (!apply) {
		// Record everything undo needs before anything changes, so a reset which couldn't be undone is refused.
		auto* id_stack = model_stack->addParamCollectionAndId(collection, summary, id);
		if (!existed) {
			// A recreated parameter starts at its reset value, so only its creation needs undoing.
			return recordResetEntry(action, id_stack, true, Polarity::BIPOLAR);
		}
		auto* stack = collection->getAutoParamFromId(id_stack, false);
		if (stack->autoParam == nullptr) {
			return Error::NONE;
		}
		// An unautomated route with a zero baseline is deleted, and undo recreates it with its default polarity.
		if (collection->getParamKind() == deluge::modulation::params::Kind::PATCH_CABLE
		    && !stack->autoParam->isAutomated() && scalar->getPresetValue() == 0) {
			auto* cables = static_cast<PatchCableSet*>(collection);
			PatchSource source = PatchSource::NOT_AVAILABLE;
			ParamDescriptor destination;
			PatchCableSet::dissectParamId(id, &destination, &source);
			const PatchCable& cable = cables->patchCables[cables->getPatchCableIndex(source, destination)];
			if (cable.polarity != PatchCable::getDefaultPolarity(cable.from)) {
				Error error = recordResetEntry(action, stack, false, cable.polarity);
				if (error != Error::NONE) {
					return error;
				}
			}
		}
		return snapshotResetValue(action, stack);
	}
	auto* stack = collection->getAutoParamFromId(model_stack->addParamCollectionAndId(collection, summary, id), create);
	if (stack->autoParam == nullptr) {
		return create ? Error::INSUFFICIENT_RAM : Error::NONE;
	}
	scalar = collection->getClipValueStorage(id);
	if (scalar == nullptr) {
		return Error::NONE;
	}
	int32_t old_value = existed ? stack->autoParam->getCurrentValue() : 0;
	bool automated = stack->autoParam->isAutomated();
	// Restore the clip scalar directly rather than through the recording-sensitive control setter,
	// so reset applies even while recording or playing.
	scalar->setValue(scalar->getPresetValue());
	notifyResetValue(collection, stack, old_value, automated);
	return Error::NONE;
}
template <typename Reset>
PLACE_SDRAM_TEXT Error resetPatchCableValues(PatchCableSet* cables, const PatchCablePresetValues& preset,
                                             const Reset& reset) {
	// Reset can delete zero-strength routes and reorder storage. Use stable
	// route IDs, including routes removed since the preset was loaded.
	std::array<int32_t, kMaxNumPatchCables * 2> ids_storage{};
	int32_t* ids = ids_storage.data();
	int32_t count = 0;
	for (int32_t c = 0; c < cables->numPatchCables; ++c) {
		auto& cable = cables->patchCables[c];
		ids[count++] = PatchCableSet::getParamId(cable.destinationParamDescriptor, cable.from);
	}
	for (int32_t c = 0; c < preset.count; ++c) {
		bool found = false;
		for (int32_t i = 0; i < count; ++i) {
			found |= static_cast<uint32_t>(ids[i]) == preset.entries[c].id;
		}
		if (!found && (preset.entries[c].value != 0)) {
			ids[count++] = static_cast<int32_t>(preset.entries[c].id);
		}
	}
	for (int32_t i = 0; i < count; ++i) {
		Error error = reset(ids[i], *preset.find(ids[i]) != 0);
		if (error != Error::NONE) {
			return error;
		}
	}
	return Error::NONE;
}
} // namespace

PLACE_SDRAM_TEXT Error ParamManager::resetValuesToPreset(ModelStackWithThreeMainThings* modelStack, Action& action,
                                                         bool apply) {
	if (modelStack->modControllable == nullptr) {
		return Error::NONE;
	}
	using deluge::modulation::params::Kind;
	modelStack->modControllable->bindPresetValues(this);
	for (auto* summary = summaries; summary->paramCollection != nullptr; ++summary) {
		auto* collection = summary->paramCollection;
		auto kind = collection->getParamKind();
		// Expression is not a preset parameter collection.
		if (kind != Kind::PATCHED && kind != Kind::UNPATCHED_SOUND && kind != Kind::UNPATCHED_GLOBAL
		    && kind != Kind::MIDI && kind != Kind::PATCH_CABLE) {
			continue;
		}
		auto reset = [&](int32_t id, bool create) {
			return resetParameterValue(collection, summary, modelStack, id, create, action, apply);
		};
		Error error = Error::NONE;
		if (kind == Kind::PATCH_CABLE) {
			const auto& preset =
			    static_cast<ModControllableAudio*>(modelStack->modControllable)->getPresetPatchCableValues();
			error = resetPatchCableValues(static_cast<PatchCableSet*>(collection), preset, reset);
		}
		else if (kind == Kind::MIDI) {
			auto* instrument = static_cast<MIDIInstrument*>(modelStack->modControllable);
			for (int32_t cc = 0; cc < kNumCCNumbersIncludingFake && error == Error::NONE; ++cc) {
				error = reset(cc, instrument->getPresetMidiValue(cc) != 0);
			}
		}
		else {
			auto* set = static_cast<ParamSet*>(collection);
			for (int32_t id = 0; id < set->getNumParams() && error == Error::NONE; ++id) {
				error = reset(id, false);
			}
		}
		if (error != Error::NONE) {
			return error;
		}
	}
	return Error::NONE;
}
