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
class ModelStackWithThreeMainThings;
class ClipParamValue;
class ModelStackWithParamId;
class ModelStackWithParamCollection;
class ParamCollectionSummary;

class ParamCollection {
public:
	ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary);
	virtual ~ParamCollection();

	virtual ClipParamValue* getClipValueStorage(int32_t /*paramId*/) { return nullptr; }
	static Output* getLinkedOutput(ModelStackWithThreeMainThings const* modelStack);
	// The same for a control of one parameter. While a quantized stutter runs, its rate control edits the
	// stutter's centred clip value rather than the shared baseline.
	static Output* getLinkedOutput(ModelStackWithThreeMainThings const* modelStack,
	                               deluge::modulation::params::Kind kind, int32_t id);
	static Output* getLinkedOutput(ModelStackWithParamId const* modelStack);
	static bool controlsEditClipValue(ModelStackWithThreeMainThings const* modelStack);
	// Collections choose which value layer their normal panel controls edit.
	int32_t getValueForControl(ModelStackWithParamId const* modelStack);
	virtual int32_t getValueForControl(int32_t paramId, ModelStackWithThreeMainThings const* modelStack) = 0;
	void setValueFromControl(int32_t value, ModelStackWithParamId const* modelStack, int32_t pos = 0,
	                         int32_t length = 0, bool mayDeleteNodesInLinearRun = true);

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
	// Announces that a parameter's value and/or automation may have changed. If the parameter lost its
	// automation, first removes its automation offset; then reads its current value and passes it to
	// notifyParamValueModified(). Watch the heck out! This might delete the AutoParam in question.
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);
	// The same announcement, for callers which already know the parameter's new current value, such as
	// playback ticks. Does not clear the automation offset. Watch the heck out! This might delete the
	// AutoParam in question: PatchCableSet deletes a cable whose strength and automation become zero.
	virtual void notifyParamValueModified(ModelStackWithAutoParam const* modelStack, int32_t oldValue, int32_t newValue,
	                                      bool automationChanged, bool automatedBefore, bool automatedNow);
	virtual ModelStackWithAutoParam* getAutoParamFromId(
	    ModelStackWithParamId* modelStack,
	    bool allowCreation = false) = 0; // You must not pass this any child class of ModelStackWithThreeMoreThings
	                                     // (wait why again?). May return NULL

	virtual bool mayParamInterpolate(int32_t paramId);
	virtual bool shouldInterpolateWithFloat(ModelStackWithParamId const* modelStack) { return false; }
	virtual bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) { return false; }
	virtual bool doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack) { return true; }
	virtual bool shouldRecordUnautomatedParamChange(ModelStackWithParamId const* modelStack) { return true; }
	virtual int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	virtual int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack);
	virtual void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack);
	virtual deluge::modulation::params::Kind getParamKind() = 0;

	const int32_t objectSize;
	int32_t ticksTilNextEvent;

protected:
	void setRenderValues(int32_t* values) {
		renderValues_ = values;
		renderVersion_ = 0;
	}
	[[nodiscard]] int32_t* renderValues() const { return renderValues_; }
	[[nodiscard]] uint32_t* renderVersionStorage() const { return &renderVersion_; }
	// The step array, allocated on first use; null if that failed.
	int32_t* rampSteps(int32_t count) { return (rampSteps_ != nullptr) ? rampSteps_ : allocateRampSteps(count); }
	// For a clone, whose copied pointer is its source's.
	void forgetRampSteps() { rampSteps_ = nullptr; }

private:
	friend class ParamRenderValues;
	// For collections rendering reads through ParamRenderValues: the values resolved from the
	// layers, refreshed whenever parameter_value_epoch moves. Playback loops keep the parameters
	// they advance up to date.
	int32_t* renderValues_ = nullptr;
	mutable uint32_t renderVersion_ = 0;
	// Steps of ramps held for parameters with interpolating automation, indexed like
	// renderValues_. Allocated by the first tick that can hold one; a clone allocates its own.
	int32_t* rampSteps_ = nullptr;
	[[gnu::noinline]] int32_t* allocateRampSteps(int32_t count);
};
