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
#include <cstdint>

#include "modulation/params/param.h"

class ParamManagerForTimeline;
class Sound;
class InstrumentClip;
class Action;
struct AutoParamState;
class ParamNode;
class AutoParam;
class CopiedParamAutomation;
class TimelineCounter;
class Output;
class Clip;
class ModelStackWithAutoParam;
class ModelStackWithParamId;
class ModelStackWithParamCollection;
class ParamCollectionSummary;

class ParamCollection {
public:
	ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary);
	virtual ~ParamCollection();

	virtual void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength = 0) = 0;

	/// tick interpolation by a number of ticks
	virtual void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) = 0;

	/// tick interpolation by a number of ticks
	virtual void tickTicks(int32_t numSamples, ModelStackWithParamCollection* modelStack) = 0;
	virtual void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed);
	virtual void playbackHasEnded(ModelStackWithParamCollection* modelStack) = 0;
	virtual void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) = 0;
	virtual void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                             bool shouldPingpong) = 0;
	virtual void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                                   ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                                   int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) = 0;
	virtual void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                          bool maySetupPatching) = 0;
	virtual void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount,
	                               int32_t effectiveLength) = 0;
	virtual void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSinceLast, bool reversed,
	                               bool didPingpong, bool mayInterpolate) = 0;
	virtual void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) = 0;
	virtual void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) = 0;
	virtual void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                             ModelStackWithParamCollection* modelStack) = 0;
	virtual void
	notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool automationChanged,
	                             bool automatedBefore,
	                             bool automatedNow); // Watch the heck out! This might delete the AutoParam in question.
	virtual ModelStackWithAutoParam* getAutoParamFromId(
	    ModelStackWithParamId* modelStack,
	    bool allowCreation = false) = 0; // You must not pass this any child class of ModelStackWithThreeMoreThings
	                                     // (wait why again?). May return NULL

	virtual bool mayParamInterpolate(int32_t paramId);
	virtual bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) { return false; }
	virtual bool doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack) { return true; }
	virtual int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	virtual int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack);
	virtual void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack);
	virtual deluge::modulation::params::Kind getParamKind() = 0;

	const int32_t objectSize;
	int32_t ticksTilNextEvent;
};
