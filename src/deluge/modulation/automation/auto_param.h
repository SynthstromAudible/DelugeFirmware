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
#include "model/action/action.h"
#include "modulation/automation/auto_param_data.h"
#include <type_traits>

class ModelStackWithAutoParam;
class CopiedParamAutomation;
struct StolenParamNodes;

class AutoParamSetup;
class AutoParamOwnership;
class AutoParamPlayback;
class AutoParamAutomation;
class AutoParamLiveInput;

// Runtime parameters expose queries and explicit curve editing. Privileged callers
// use role-specific interfaces; none are friends of the complete parameter class.
class AutoParam : private AutoParamData {
	friend class AutoParamSetup;
	friend class AutoParamOwnership;
	friend class AutoParamPlayback;
	friend class AutoParamAutomation;
	friend class AutoParamLiveInput;

public:
	AutoParam() = default;
	[[nodiscard]] const AutoParamData& data() const { return *this; }
	AutoParamAutomation automation();
	using AutoParamData::nodeAt;
	using AutoParamData::nodeCount;
	using AutoParamData::nodeStorageIdentity;
	using AutoParamData::searchNodes;

	using AutoParamData::getCurrentValue;
	using AutoParamData::getValueAtPos;
	using AutoParamData::isAutomated;
	using AutoParamData::writeToFile;
	int32_t getValueAtPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed = false);
	bool containsSomething(uint32_t neutralValue = 0);
	static bool containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue = 0);
	int32_t getDistanceToNextNode(ModelStackWithAutoParam const* modelStack, int32_t pos, bool reversed);
	int32_t getValuePossiblyAtPos(int32_t pos, ModelStackWithAutoParam* modelStack);
	bool hasInterpolationIncrement(bool use_float_interpolation);

private:
	using AutoParamData::grabValueFromPos;
	void setValueForLiveInput(int32_t value, ModelStackWithAutoParam const* modelStack, int32_t livePos = -1,
	                          bool mayDeleteNodesInLinearRun = true, bool doMPEMode = false);
	void setLiveValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const* modelStack, int32_t pos,
	                                   int32_t length, bool mayDeleteNodesInLinearRun = true);
	int32_t processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed, bool didPinpong,
	                          bool mayInterpolate = true, bool mustUpdateValueAtEveryNode = false);
	void setValueForRegion(uint32_t pos, uint32_t length, int32_t value, ModelStackWithAutoParam const* modelStack,
	                       ActionType actionType = ActionType::NOTE_EDIT);
	/// tick the interpolator by a number of samples - used for internal synths
	bool tickSamples(int32_t numSamples, bool use_float_interpolation);
	/// tick the interpolator by a number of ticks - used for midi
	bool tickTicks(int32_t numTicks, bool use_float_interpolation);
	void setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed);
	bool grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack);
	void generateRepeats(uint32_t oldLength, uint32_t newLength, bool shouldPingpong);
	Error beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength);
	void copyOverridingFrom(AutoParam* otherParam);
	void trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack);
	void deleteAutomation(Action* action, ModelStackWithAutoParam const* modelStack, bool shouldNotify = true);
	void shiftValues(int32_t offset);
	void shiftParamVolumeByDB(float offset);
	void shiftHorizontally(int32_t amount, int32_t effectiveLength);
	void swapState(AutoParamState* state, ModelStackWithAutoParam const* modelStack);
	void copy(int32_t startPos, int32_t endPos, CopiedParamAutomation* copiedParamAutomation, bool isPatchCable,
	          ModelStackWithAutoParam const* modelStack);
	void paste(int32_t startPos, int32_t endPos, float scaleFactor, ModelStackWithAutoParam const* modelStack,
	           CopiedParamAutomation* copiedParamAutomation, bool isPatchCable);
	Error makeInterpolationGoodAgain(int32_t clipLength, int32_t quantizationRShift);
	void transposeCCValuesToChannelPressureValues();
	void deleteTime(int32_t startPos, int32_t lengthToDelete, ModelStackWithAutoParam* modelStack);
	void insertTime(int32_t pos, int32_t lengthToInsert);
	void appendParam(AutoParam* otherParam, int32_t oldLength, int32_t reverseThisRepeatWithLength,
	                 bool pingpongingGenerally);
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithAutoParam const* modelStack);
	void stealNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength, int32_t loopLength,
	                Action* action, StolenParamNodes* stolenNodeRecord = nullptr);
	void insertStolenNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
	                       int32_t loopLength, Action* action, StolenParamNodes* stolenNodeRecord);
	void moveRegionHorizontally(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t length, int32_t offset,
	                            int32_t lengthBeforeLoop, Action* action);
	void deleteNodesWithinRegion(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t length);
	int32_t homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos, int32_t length,
	                         int32_t startValue, bool interpolateLeftNode, bool interpolateRightNode,
	                         int32_t effectiveLength, bool reversed, int32_t posAtWhichClipWillCut = 2147483647);
	void setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t value);
	void notifyPingpongOccurred(bool use_float_interpolation);
	bool deleteRedundantNodeInLinearRun(int32_t lastNodeInRunI, int32_t effectiveLength,
	                                    bool mayLoopAroundBackToEnd = true);
	void setupInterpolation(ModelStackWithAutoParam const* modelStack, ParamNode* nextNode, int32_t effectiveLength,
	                        int32_t currentPos, bool reversed);
	void homogenizeRegionTestSuccess(int32_t pos, int32_t regionEnd, int32_t startValue, bool interpolateStart,
	                                 bool interpolateEnd);
	void deleteNodesBeyondPos(int32_t pos);
	bool useFloatInterpolation(ModelStackWithAutoParam const* modelStack);
	void calculateInterpolationIncrement(int32_t half_distance, int32_t ticks_til_next_node,
	                                     bool use_float_interpolation);
	void potentiallyOverrideInterpolationIncrement(int32_t limit, bool use_float_interpolation);
	void reverseInterpolationIncrement(bool use_float_interpolation);
	int32_t consumeFloatInterpolationIncrement(float value_increment);
	bool applyValueIncrement(int32_t value_increment);

	void cloneFrom(AutoParam* other, bool copyAutomation) { AutoParamData::cloneFrom(other, copyAutomation); }
};
static_assert(sizeof(AutoParam) == sizeof(AutoParamData));
static_assert(std::is_standard_layout_v<AutoParam>);
static_assert(alignof(AutoParam) == alignof(AutoParamData));
