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
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "storage/storage_manager.h"
#include <array>

class Sound;
class ParamManagerForTimeline;
class TimelineCounter;
class ModelStackWithParamCollection;

// ParamSet specifies a lot of stuff about how the params will be stored - there's always a fixed number, and they don't
// need other info stored besides their index - like MIDI CC, or patch cable details. This differs from other inheriting
// classes of ParamCollection.

class ParamSet : public ParamCollection {
protected:
	/// Number of parameters in the params array
	int32_t numParams_;

public:
	AutoParam* params;
	ParamSet(int32_t newObjectSize, ParamCollectionSummary* summary);

	inline int32_t getValue(int32_t p) { return params[p].getCurrentValue(); }
	int32_t getValueAtPos(int32_t p, uint32_t pos, TimelineCounter* playPositionCounter);
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
	                       bool didPingpong, bool mayInterpolate) final;
	void writeParamAsAttribute(Serializer& writer, char const* name, int32_t p, bool writeAutomation,
	                           bool onlyIfContainsSomething = false, int32_t* valuesForOverride = NULL);
	void readParam(Deserializer& reader, ParamCollectionSummary* summary, int32_t p, int32_t readAutomationUpToPos);
	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) final;
	void tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) final;
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) final;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) final;
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) final;
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong) final;
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) final;
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override;
	void cloneFrom(ParamCollection* otherParamSet, bool copyAutomation);
	void copyOverridingFrom(ParamSet* otherParamSet);
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching) final;
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) final;
	void paramHasAutomationNow(ParamCollectionSummary* summary, int32_t p);
	void paramHasNoAutomationNow(ModelStackWithParamCollection const* modelStack, int32_t p);

	void shiftParamValues(int32_t p, int32_t offset);
	void shiftParamVolumeByDB(int32_t p, float offset);
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength) final;
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	void deleteAutomationForParamBasicForSetup(ModelStackWithParamCollection* modelStack, int32_t p);
	void insertTime(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t lengthToInsert);
	void deleteTime(ModelStackWithParamCollection* modelStack, int32_t startPos, int32_t lengthToDelete);
	void backUpAllAutomatedParamsToAction(Action* action, ModelStackWithParamCollection* modelStack);
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) final;

	// For undoing / redoing
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) final;

	int32_t getNumParams() { return numParams_; }

	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = true) final;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;

	uint8_t topUintToRepParams;

private:
	void backUpParamToAction(int32_t p, Action* action, ModelStackWithParamCollection* modelStack);
	void checkWhetherParamHasInterpolationNow(ModelStackWithParamCollection const* modelStack, int32_t p);
};

class UnpatchedParamSet final : public ParamSet {
public:
	UnpatchedParamSet(ParamCollectionSummary* summary);
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override;
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override;
	bool doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	deluge::modulation::params::Kind getParamKind() override { return kind; }

	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;

private:
	std::array<AutoParam, deluge::modulation::params::kMaxNumUnpatchedParams> params_;
};

class PatchedParamSet final : public ParamSet {
public:
	PatchedParamSet(ParamCollectionSummary* summary);
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override;
	deluge::modulation::params::Kind getParamKind() { return deluge::modulation::params::Kind::PATCHED; }

private:
	std::array<AutoParam, deluge::modulation::params::kNumParams> params_;
};

class ExpressionParamSet final : public ParamSet {
public:
	ExpressionParamSet(ParamCollectionSummary* summary, bool forDrum = false);
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	bool mayParamInterpolate(int32_t paramId) override { return false; }
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	bool writeToFile(Serializer& writer, bool mustWriteOpeningTagEndFirst = false);
	void readFromFile(Deserializer& reader, ParamCollectionSummary* summary, int32_t readAutomationUpToPos);
	void moveRegionHorizontally(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t length, int32_t offset,
	                            int32_t lengthBeforeLoop, Action* action);
	void clearValues(ModelStackWithParamCollection const* modelStack);
	void cancelAllOverriding();
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	deluge::modulation::params::Kind getParamKind() { return deluge::modulation::params::Kind::EXPRESSION; }

	// bendRanges being stored here in ExpressionParamSet still seems like the best option. I was thinking storing them
	// in the ParamManager would make more sense, except for one thing
	// - persistence when preset/Instrument changes. ExpressionParamSets do this unique thing where they normally aren't
	// "stolen" or "backed up" - unless the last Clip is being deleted, in which case they do move to the
	// backedUpParamManager. This is exactly the persistence we want for bendRanges too.
	uint8_t bendRanges[2];

private:
	std::array<AutoParam, kNumExpressionDimensions> params_;
};
