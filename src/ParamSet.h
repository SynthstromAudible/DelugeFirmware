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

#ifndef PARAMSET_H_
#define PARAMSET_H_

#include "AutoParam.h"
#include "definitions.h"
#include "ParamCollection.h"

class Sound;
class ParamManagerForTimeline;
class ParamManagerForTimeline;
class ParamManagerForTimeline;
class ParamManagerMIDI;
class TimelineCounter;
class ModelStackWithParamCollection;

// ParamSet specifies a lot of stuff about how the params will be stored - there's always a fixed number, and they don't need other info stored besides their index - like MIDI CC,
// or patch cable details.
// This differs from other inheriting classes of ParamCollection.

class ParamSet : public ParamCollection {
public:
	ParamSet(int newObjectSize, ParamCollectionSummary* summary);

	inline int32_t getValue(int p) { return params[p].getCurrentValue(); }
	int32_t getValueAtPos(int p, uint32_t pos, TimelineCounter* playPositionCounter);
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int ticksSkipped, bool reversed, bool didPingpong,
	                       bool mayInterpolate) final;
	void writeParamAsAttribute(char const* name, int p, bool writeAutomation, bool onlyIfContainsSomething = false,
	                           int32_t* valuesForOverride = NULL);
	void readParam(ParamCollectionSummary* summary, int p, int32_t readAutomationUpToPos);
	void tickSamples(int numSamples, ModelStackWithParamCollection* modelStack) final;
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) final;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) final;
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) final;
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong) final;
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) final;
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength);
	void cloneFrom(ParamCollection* otherParamSet, bool copyAutomation);
	void copyOverridingFrom(ParamSet* otherParamSet);
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching) final;
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) final;
	void paramHasAutomationNow(ParamCollectionSummary* summary, int p);
	void paramHasNoAutomationNow(ModelStackWithParamCollection const* modelStack, int p);

	void shiftParamValues(int p, int32_t offset);
	void shiftParamVolumeByDB(int p, float offset);
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength) final;
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack);
	void deleteAutomationForParamBasicForSetup(ModelStackWithParamCollection* modelStack, int p);
	void insertTime(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t lengthToInsert);
	void deleteTime(ModelStackWithParamCollection* modelStack, int32_t startPos, int32_t lengthToDelete);
	void backUpAllAutomatedParamsToAction(Action* action, ModelStackWithParamCollection* modelStack);
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) final;

	// For undoing / redoing
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) final;

	virtual int getNumParams() = 0;

	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = true) final;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);

	uint8_t topUintToRepParams;

	AutoParam params
	    [1]; // Total hack - we declare this last, then the subclasses "extend" it by having extra unused space after it

private:
	void backUpParamToAction(int p, Action* action, ModelStackWithParamCollection* modelStack);
	void checkWhetherParamHasInterpolationNow(ModelStackWithParamCollection const* modelStack, int p);
};

class UnpatchedParamSet final : public ParamSet {
public:
	UnpatchedParamSet(ParamCollectionSummary* summary);
	int getNumParams() { return MAX_NUM_UNPATCHED_PARAMS; }
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack);
	bool doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack);

	AutoParam fakeParams[MAX_NUM_UNPATCHED_PARAMS - 1];
};

class PatchedParamSet final : public ParamSet {
public:
	PatchedParamSet(ParamCollectionSummary* summary);
	int getNumParams() { return NUM_PARAMS; }
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);
	int paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	int32_t knobPosToParamValue(int knobPos, ModelStackWithAutoParam* modelStack);
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack);

	AutoParam fakeParams[NUM_PARAMS - 1];
};

class ExpressionParamSet final : public ParamSet {
public:
	ExpressionParamSet(ParamCollectionSummary* summary, bool forDrum = false);
	int getNumParams() { return NUM_EXPRESSION_DIMENSIONS; }
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);
	bool mayParamInterpolate(int paramId) { return false; }
	int32_t knobPosToParamValue(int knobPos, ModelStackWithAutoParam* modelStack);
	int paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	bool writeToFile(bool mustWriteOpeningTagEndFirst = false);
	void readFromFile(ParamCollectionSummary* summary, int32_t readAutomationUpToPos);
	void moveRegionHorizontally(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t length, int offset,
	                            int32_t lengthBeforeLoop, Action* action);
	void clearValues(ModelStackWithParamCollection const* modelStack);
	void cancelAllOverriding();
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack);

	AutoParam fakeParams[NUM_EXPRESSION_DIMENSIONS - 1];

	// bendRanges being stored here in ExpressionParamSet still seems like the best option. I was thinking storing them in the ParamManager would make more sense, except for one thing
	// - persistence when preset/Instrument changes. ExpressionParamSets do this unique thing where they normally aren't "stolen" or "backed up" - unless the last Clip is being deleted,
	// in which case they do move to the backedUpParamManager. This is exactly the persistence we want for bendRanges too.
	uint8_t bendRanges[2];
};

#endif /* PARAMSET_H_ */
