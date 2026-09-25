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
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include <algorithm>
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

ModelStackWithParamCollection* ParamManager::getPatchCableSet(ModelStackWithThreeMainThings const* modelStack) {
#if ALPHA_OR_BETA_VERSION
	if (param_kind_at_offset(2) != deluge::modulation::params::Kind::PATCH_CABLE) {
		FREEZE_WITH_ERROR("PM08"); // was E412
	}
#endif
	return modelStack->addParamCollection(summaries[2].paramCollection, &summaries[2]);
}

// Even if it's just expression params.
void ParamManagerForTimeline::ensureSomeParamCollections() {
#if ALPHA_OR_BETA_VERSION
	if (!matches_type(ParamManagerType::ANY) || !has_valid_layout()) {
		FREEZE_WITH_ERROR("PM0A"); // was E408
	}
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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->setPlayPos(pos, modelStackWithParamCollection, reversed);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	expectEvent(modelStack);
	ticksSkipped = 0;
}

void ParamManagerForTimeline::grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack) {

#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START

	summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, effectiveLength);

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END
}

void ParamManagerForTimeline::deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
	otherModelStack->paramManager->toForTimeline()
	    ->ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do"
	                                    // below with its "while".
#endif

	ParamCollectionSummary* otherSummary = otherModelStack->paramManager->summaries;
	ParamCollectionSummary* summary = summaries;
	do {
		// The source may introduce automation into a previously empty collection.
		if (otherSummary->containsAutomation()) {
			auto* destinationStack = modelStack->addParamCollectionSummary(summary);
			auto* sourceStack = otherModelStack->addParamCollectionSummary(otherSummary);
			summary->paramCollection->appendParamCollection(destinationStack, sourceStack, oldLength,
			                                                reverseThisRepeatWithLength, pingpongingGenerally);
		}
		summary++;
		otherSummary++;
	} while (summary->paramCollection);

	ticksTilNextEvent = 0; // Should probably really call expectEvent(), but we're only called when a tick is just about
	                       // to happen anyway, so shouldn't matter
}

// Note: you must only call this if playbackHandler.isEitherClockActive()
void ParamManagerForTimeline::tickSamples(int32_t numSamples, ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
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
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_START
	summary->paramCollection->notifyPingpongOccurred(modelStackWithParamCollection);
	FOR_EACH_AUTOMATED_PARAM_COLLECTION_DEFINITELY_SOME_END

	ticksTilNextEvent = 0;
}

void ParamManagerForTimeline::expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack) {
#if ALPHA_OR_BETA_VERSION
	ensureSomeParamCollections(); // If you're going to delete this and allow none, make sure you replace the "do" below
	                              // with its "while".
#endif

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