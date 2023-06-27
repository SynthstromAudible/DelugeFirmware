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

#include <InstrumentClip.h>
#include <ParamManager.h>
#include "View.h"
#include "song.h"
#include "ParamCollection.h"
#include "playbackhandler.h"
#include "ModelStack.h"
#include "GeneralMemoryAllocator.h"
#include "ParamSet.h"
#include "MIDIParamCollection.h"
#include "PatchCableSet.h"
#include <new>

ParamManager::ParamManager() {
	summaries[0] = {0};
#if ALPHA_OR_BETA_VERSION
	summaries[1] = {0};
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
#endif
	expressionParamSetOffset = 0;

	resonanceBackwardsCompatibilityProcessed = false;
}

ParamManager::~ParamManager() {
	destructAndForgetParamCollections();
}

#if ALPHA_OR_BETA_VERSION
ParamManagerForTimeline* ParamManager::toForTimeline() {
	numericDriver.freezeWithError("E407");
	return NULL;
}

ParamManagerForTimeline* ParamManagerForTimeline::toForTimeline() {
	return this;
}
#endif

int ParamManager::setupMIDI() {
	void* memory = generalMemoryAllocator.alloc(sizeof(MIDIParamCollection), NULL, false, true);
	if (!memory) return ERROR_INSUFFICIENT_RAM;

	summaries[1] = summaries[0]; // Potentially shuffle the expression params over.
	summaries[0].paramCollection = new (memory) MIDIParamCollection(&summaries[0]);
	summaries[2] = {0};
	expressionParamSetOffset = 1;
	return NO_ERROR;
}

int ParamManager::setupUnpatched() {
	void* memoryUnpatched = generalMemoryAllocator.alloc(sizeof(UnpatchedParamSet), NULL, false, true);
	if (!memoryUnpatched) return ERROR_INSUFFICIENT_RAM;

	summaries[0].paramCollection = new (memoryUnpatched) UnpatchedParamSet(&summaries[0]);
	summaries[1] = {0};
	expressionParamSetOffset = 1;
	return NO_ERROR;
}

int ParamManager::setupWithPatching() {
	void* memoryUnpatched = generalMemoryAllocator.alloc(sizeof(UnpatchedParamSet), NULL, false, true);
	if (!memoryUnpatched) return ERROR_INSUFFICIENT_RAM;

	void* memoryPatched = generalMemoryAllocator.alloc(sizeof(PatchedParamSet), NULL, false, true);
	if (!memoryPatched) {
ramError2:
		generalMemoryAllocator.dealloc(memoryUnpatched);
		return ERROR_INSUFFICIENT_RAM;
	}

	void* memoryPatchCables = generalMemoryAllocator.alloc(sizeof(PatchCableSet), NULL, false, true);
	if (!memoryPatchCables) {
		generalMemoryAllocator.dealloc(memoryPatched);
		goto ramError2;
	}

	summaries[0].paramCollection = new (memoryUnpatched) UnpatchedParamSet(&summaries[0]);
	summaries[1].paramCollection = new (memoryPatched) PatchedParamSet(&summaries[1]);
	summaries[2].paramCollection = new (memoryPatchCables) PatchCableSet(&summaries[2]);
	summaries[3] = {0};
	expressionParamSetOffset = 3;
	return NO_ERROR;
}

// Make sure other isn't NULL before you call this, you muppet.
void ParamManager::stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams) {
#if ALPHA_OR_BETA_VERSION
	if (!other) numericDriver.freezeWithError("E413");
#endif

	int mpeParamsOffsetOther = other->getExpressionParamSetOffset();
	int mpeParamsOffsetHere = getExpressionParamSetOffset();
	int stopAtOther = mpeParamsOffsetOther;

	// If we're planning to steal expression params, and yes "other" does in fact have them...
	if (stealExpressionParams && other->summaries[stopAtOther].paramCollection) {

		// If "here" has them too, we'll just keep these, and destruct "other"'s ones
		if (summaries[mpeParamsOffsetHere].paramCollection) {
			other->summaries[stopAtOther].paramCollection->~ParamCollection();
			generalMemoryAllocator.dealloc(other->summaries[stopAtOther].paramCollection);
			other->summaries[stopAtOther] = {0};
		}

		// Otherwise, yup, proceed to steal them
		else {
			stopAtOther++;
		}
	}

	ParamCollectionSummary hereMpeParamsOrNull = summaries[mpeParamsOffsetHere];

	int i;
	for (i = 0; i < stopAtOther; i++) {
		summaries[i] = other->summaries[i];
	}

	summaries[stopAtOther] = hereMpeParamsOrNull; // Could the expression params, or NULL
	if (hereMpeParamsOrNull.paramCollection)
		summaries[stopAtOther + 1] = {0}; // If that was expression params, write the actual terminating NULL here
		                                  // - but not otherwise, cos we could have overflowed past the array's size!
	expressionParamSetOffset = mpeParamsOffsetOther;

	other->summaries[0] = other->summaries[stopAtOther];
	other->summaries[1] = {0};
#if ALPHA_OR_BETA_VERSION
	other->summaries[2] = {0};
	other->summaries[3] = {0};
	other->summaries[4] = {0};
#endif
	other->expressionParamSetOffset = 0;
}

int ParamManager::cloneParamCollectionsFrom(ParamManager* other, bool copyAutomation, bool cloneExpressionParams,
                                            int32_t reverseDirectionWithLength) {

	ParamCollectionSummary mpeParamsOrNullHere = *getExpressionParamSetSummary();
	if (mpeParamsOrNullHere.paramCollection)
		cloneExpressionParams = false; // If we already have expression params, then just don't clone from "other".

	// First, allocate the memories
	ParamCollectionSummary newSummaries
	    [PARAM_COLLECTIONS_STORAGE_NUM]; // Temporary separate storage, so we can clone from self (when this function is called from beenCloned()).

	ParamCollectionSummary* __restrict__ newSummary = newSummaries;
	ParamCollectionSummary* otherSummary =
	    other->summaries; // Not __restrict__, because other might be the same as this!
	ParamCollectionSummary const* otherStopAt = &other->summaries[other->expressionParamSetOffset];

	if (cloneExpressionParams && otherStopAt->paramCollection) otherStopAt++;

	while (otherSummary != otherStopAt) {

		newSummary->paramCollection = (ParamCollection*)generalMemoryAllocator.alloc(
		    otherSummary->paramCollection->objectSize, NULL, false,
		    true); // To cut corners, we store this currently blank/undefined memory in our array of type ParamCollectionSummary

		// If that failed, deallocate all the previous memories
		if (!newSummary->paramCollection) {
			while (newSummary != newSummaries) {
				newSummary--;
				generalMemoryAllocator.dealloc(newSummary->paramCollection);
			}

			// Mark that there's nothing here
			summaries[0] = mpeParamsOrNullHere;
			summaries[1] = {0};
			return ERROR_INSUFFICIENT_RAM;
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

	*newSummary = mpeParamsOrNullHere;
	if (mpeParamsOrNullHere.paramCollection) { // Check first, otherwise we'll overflow the array, I think...
		newSummary++;
		*newSummary = {0}; // Mark end of list
	}

	// And finally, copy the pointers and flags from newSummaries to our permanent summaries array.
	newSummary = newSummaries;
	ParamCollectionSummary* destSummaries = summaries;
	while (true) {
		*destSummaries = *newSummary;
		if (!newSummary->paramCollection) break;
		destSummaries++;
		newSummary++;
	}

	expressionParamSetOffset = other->expressionParamSetOffset;

	return NO_ERROR;
}

// This is only called once - for NoteRows after cloning an InstrumentClip.
int ParamManager::beenCloned(int32_t reverseDirectionWithLength) {
	return cloneParamCollectionsFrom(this, true, true, reverseDirectionWithLength); // *Does* clone expression params
}

// Does *not* forget MPE params
void ParamManager::forgetParamCollections() {
	summaries[0].paramCollection = getExpressionParamSet();
	summaries[1] = {0};
	expressionParamSetOffset = 0;
#if ALPHA_OR_BETA_VERSION
	summaries[2] = {0};
	summaries[3] = {0};
	summaries[4] = {0};
#endif
}

// This one deletes MPE params too
void ParamManager::destructAndForgetParamCollections() {
	ParamCollectionSummary* summary = summaries;
	while (summary->paramCollection) {
		summary->paramCollection->~ParamCollection();
		generalMemoryAllocator.dealloc(summary->paramCollection);
		summary++;
	}

	summaries[0] = {0};
	expressionParamSetOffset = 0;
}

// Returns whether there is one / one could be created.
bool ParamManager::ensureExpressionParamSetExists(bool forDrum) {
	int offset = getExpressionParamSetOffset();
	if (!summaries[offset].paramCollection) {

		void* memory = generalMemoryAllocator.alloc(sizeof(ExpressionParamSet), NULL, false, true);
		if (!memory) return false;

		summaries[offset].paramCollection = new (memory) ExpressionParamSet(&summaries[offset], forDrum);
		summaries[offset + 1] = {0};
	}
	return true;
}

ExpressionParamSet* ParamManager::getOrCreateExpressionParamSet(bool forDrum) {
	if (!ensureExpressionParamSetExists(forDrum)) return NULL;

	return getExpressionParamSet();
}

ModelStackWithParamCollection* ParamManager::getPatchCableSet(ModelStackWithThreeMainThings const* modelStack) {
#if ALPHA_OR_BETA_VERSION
	if (!summaries[2].paramCollection) numericDriver.freezeWithError("E412");
#endif
	return modelStack->addParamCollection(summaries[2].paramCollection, &summaries[2]);
}

ParamManagerForTimeline::ParamManagerForTimeline() {
	ticksSkipped = 0;
	ticksTilNextEvent = 0;
}

// Even if it's just expression params.
void ParamManagerForTimeline::ensureSomeParamCollections() {
#if ALPHA_OR_BETA_VERSION
	if (!summaries[0].paramCollection) numericDriver.freezeWithError("E408");
#endif
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
void ParamManagerForTimeline::processCurrentPos(ModelStackWithThreeMainThings* modelStack, int ticksSinceLast,
                                                bool reversed, bool didPingpong, bool mayInterpolate) {

#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	ticksSkipped += ticksSinceLast;
	ticksTilNextEvent -= ticksSinceLast;

	if (ticksTilNextEvent <= 0) {

		ticksTilNextEvent = 2147483647;

		FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

		summary->paramCollection->processCurrentPos(modelStackWithParamCollection, ticksSkipped, reversed, didPingpong,
		                                            mayInterpolate);
		ticksTilNextEvent = getMin(ticksTilNextEvent, summary->paramCollection->ticksTilNextEvent);

		FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

		ticksSkipped = 0;
	}
}

void ParamManagerForTimeline::expectEvent(ModelStackWithThreeMainThings const* modelStack) {
	TimelineCounter* timelineCounter = modelStack->getTimelineCounterAllowNull();
	if (playbackHandler.isEitherClockActive() && (!timelineCounter || timelineCounter->isPlayingAutomationNow())) {
		ticksTilNextEvent = 0;
		if (timelineCounter) timelineCounter->expectEvent();
	}
}

// Very minimal function - doesn't take a ModelStack, because we use this to decide whether we even need to create / populate the ModelStack.
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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->setPlayPos(pos, modelStackWithParamCollection, reversed);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	expectEvent(modelStack);
	ticksSkipped = 0;
}

void ParamManagerForTimeline::grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack) {

#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->grabValuesFromPos(pos, modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManager::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int currentValueChanged,
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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, effectiveLength);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManagerForTimeline::deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->deleteAllAutomation(action, modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManagerForTimeline::trimToLength(uint32_t newLength, ModelStackWithThreeMainThings* modelStack,
                                           Action* action, bool maySetupPatching) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->trimToLength(newLength, modelStackWithParamCollection, action, maySetupPatching);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
	otherModelStack->paramManager->toForTimeline()
	    ->ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

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

ticksTilNextEvent =
    0; // Should probably really call expectEvent(), but we're only called when a tick is just about to happen anyway, so shouldn't matter
}

// Note: you must only call this if playbackHandler.isEitherClockActive()
void ParamManagerForTimeline::tickSamples(int numSamples, ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	// Beware - for efficiency, the caller of this sometimes pre-checks whether to even call this at all

	ParamCollectionSummary* summary = summaries;
	do {
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addParamCollection(summary->paramCollection, summary);
		summary->paramCollection->tickSamples(numSamples, modelStackWithParamCollection);
		summary++;
	} while (summary->paramCollection);
}

void ParamManagerForTimeline::nudgeAutomationHorizontallyAtPos(int32_t pos, int offset, int32_t lengthBeforeLoop,
                                                               Action* action,
                                                               ModelStackWithThreeMainThings* modelStack,
                                                               int32_t moveMPEDataWithinRegionLength) {

	ParamCollectionSummary* summary = summaries;
	int i = 0;
	while (summary->paramCollection) {
		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addParamCollection(summary->paramCollection, summary);

		// Special case for MPE only - not even "mono" / Clip-level expression.
		if (moveMPEDataWithinRegionLength && i == getExpressionParamSetOffset()) {
			((ExpressionParamSet*)summary->paramCollection)
			    ->moveRegionHorizontally(modelStackWithParamCollection, pos, moveMPEDataWithinRegionLength, offset,
			                             lengthBeforeLoop, action);
		}

		// Normal case
		else {
			summary->paramCollection->nudgeNonInterpolatingNodesAtPos(pos, offset, lengthBeforeLoop, action,
			                                                          modelStackWithParamCollection);
		}
		summary++;
		i++;
	}
}

void ParamManagerForTimeline::notifyPingpongOccurred(ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START
	summary->paramCollection->notifyPingpongOccurred(modelStackWithParamCollection);
	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	ticksTilNextEvent = 0;
}

void ParamManagerForTimeline::expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->playbackHasEnded(modelStackWithParamCollection);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

/*
		ParamCollection** paramCollection = paramCollections;
		do {
 			ModelStackWithParamCollection* modelStackWithParamCollection = modelStack->addParamCollection((*paramCollection));
			(*paramCollection)->
			paramCollection++;
		} while (*paramCollection);
 */
