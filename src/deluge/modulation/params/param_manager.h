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

#pragma once

#include "definitions_cxx.hpp"
#include "modulation/params/param_collection_summary.h"
#include <cstdint>

class Song;
class Sound;
class ParamSet;
class InstrumentClip;
class Action;
class ParamCollection;
class TimelineCounter;
class Output;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class ModelStackWithParamCollection;
class ExpressionParamSet;
class PatchCableSet;
class MIDIParamCollection;
class PatchedParamSet;
class UnpatchedParamSet;
class ParamManagerForTimeline;
class ParamCollectionSummary;

#define PARAM_COLLECTIONS_STORAGE_NUM 5

class ParamManager {
public:
	ParamManager();
	~ParamManager();

	// Not including MPE params
	inline bool containsAnyMainParamCollections() { return expressionParamSetOffset; }

	inline bool containsAnyParamCollectionsIncludingExpression() { return summaries[0].paramCollection; }

	Error setupWithPatching();
	Error setupUnpatched();
	Error setupMIDI();

	void stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams = false);
	Error cloneParamCollectionsFrom(ParamManager const* other, bool copyAutomation, bool cloneExpressionParams = false,
	                                int32_t reverseDirectionWithLength = 0);
	Error beenCloned(int32_t reverseDirectionWithLength = 0); // Will clone Collections
	void forgetParamCollections();
	void destructAndForgetParamCollections();
	void destructAndForgetMainParamCollections(); // Retains expression params, like forgetParamCollections()
	bool ensureExpressionParamSetExists(bool forDrum = false);

	inline int32_t getExpressionParamSetOffset() { return expressionParamSetOffset; }

	ExpressionParamSet* getOrCreateExpressionParamSet(bool forDrum = false); // Will return NULL if can't create

	inline ParamCollectionSummary* getExpressionParamSetSummary() { // Will return one containing NULL if didn't exist
		return &summaries[getExpressionParamSetOffset()];
	}

	inline ExpressionParamSet* getExpressionParamSet() { // Will return NULL if didn't exist
		return (ExpressionParamSet*)getExpressionParamSetSummary()->paramCollection;
	}

	inline MIDIParamCollection* getMIDIParamCollection() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E409");
		}
		return (MIDIParamCollection*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getMIDIParamCollectionSummary() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E409");
		}
		return &summaries[0];
	}

	inline UnpatchedParamSet* getUnpatchedParamSet() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E410");
		}
		return (UnpatchedParamSet*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getUnpatchedParamSetSummary() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E410");
		}
		return &summaries[0];
	}

	inline PatchedParamSet* getPatchedParamSet() {
		if (!summaries[1].paramCollection) {
			FREEZE_WITH_ERROR("E411");
		}
		return (PatchedParamSet*)summaries[1].paramCollection;
	}

	inline ParamCollectionSummary* getPatchedParamSetSummary() {
		if (!summaries[1].paramCollection) {
			FREEZE_WITH_ERROR("E411");
		}
		return &summaries[1];
	}

	inline ParamCollectionSummary* getPatchCableSetSummary() {
		if (!summaries[2].paramCollection) {
			FREEZE_WITH_ERROR("E412");
		}
		return &summaries[2];
	}

	inline PatchCableSet* getPatchCableSet() {
		if (!summaries[2].paramCollection) {
			FREEZE_WITH_ERROR("E412");
		}
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	ModelStackWithParamCollection* getPatchCableSet(ModelStackWithThreeMainThings const* modelStack);

	inline PatchCableSet* getPatchCableSetAllowJibberish() { // Don't ask.
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t currentValueChanged,
	                                  bool automationChanged, bool paramAutomatedNow);

	virtual ParamManagerForTimeline* toForTimeline();

	bool resonanceBackwardsCompatibilityProcessed;
	uint8_t expressionParamSetOffset;

	// This list should be terminated by an object whose values are all zero. Yes, all of them must be zero, because if
	// we know this, we can check for stuff faster.
	ParamCollectionSummary summaries[PARAM_COLLECTIONS_STORAGE_NUM];
};

class ParamManagerForTimeline final : public ParamManager { // I want to rename this one to "with automation"
public:
	ParamManagerForTimeline();

	void tickSamples(int32_t numSamples, ModelStackWithThreeMainThings* modelStack);
	void setPlayPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack, bool reversed);
	void expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack);
	void grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack);
	void generateRepeats(ModelStackWithThreeMainThings* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	void appendParamManager(ModelStackWithThreeMainThings* modelStack, ModelStackWithThreeMainThings* otherModelStack,
	                        int32_t oldLength, int32_t reverseThisRepeatWithLength, bool pingpongingGenerally);
	void trimToLength(uint32_t newLength, ModelStackWithThreeMainThings* modelStack, Action* action,
	                  bool maySetupPatching = true);

	void processCurrentPos(ModelStackWithThreeMainThings* modelStack, int32_t ticksSinceLast, bool reversed,
	                       bool didPingpong = false, bool mayInterpolate = true);
	void expectEvent(ModelStackWithThreeMainThings const* modelStack);

	void shiftHorizontally(ModelStackWithThreeMainThings* modelStack, int32_t amount, int32_t effectiveLength);
	void nudgeAutomationHorizontallyAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                      ModelStackWithThreeMainThings* modelStack, bool nudgeAutomation,
	                                      bool nudgeMPE, int32_t moveMPEDataWithinRegionLength = 0);
	void deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack);
	void notifyPingpongOccurred(ModelStackWithThreeMainThings* modelStack);
	void ensureSomeParamCollections(); // Freezes if empty - the DEFINITELY_SOME loops require at least one
	bool mightContainAutomation();

	ParamManagerForTimeline* toForTimeline() override;

	int32_t ticksTilNextEvent;
	int32_t ticksSkipped;
};
