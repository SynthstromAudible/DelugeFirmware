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
#include "model/action/action.h"
#include "modulation/params/param_node_vector.h"
#include "modulation/params/param_value.h"
#include <cstdint>

class Serializer;
class Deserializer;
class InstrumentClip;
class ParamNode;
class Action;
class ParamCollection;
class Sound;
class CopiedParamAutomation;
class TimelineCounter;
class Clip;
class ModelStackWithAutoParam;

// For backing up a snapshot
class AutoParamState {
public:
	// Existing public curve-editing interface; this object owns its nodes.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	LazyParamNodeVector nodes;
	int32_t value = 0;
	int64_t clipValue = 0;
	int64_t curveValue = 0;
	int32_t nodeBase = 0;
};

struct StolenParamNodes;

class AutoParam {
public:
	AutoParam();
	void init();

	void setCurrentValueInResponseToUserInput(int32_t value, ModelStackWithAutoParam const* modelStack,
	                                          bool shouldLogAction = true, int32_t livePos = -1,
	                                          bool mayDeleteNodesInLinearRun = true, bool isMPE = false);
	int32_t processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed, bool didPinpong,
	                          bool mayInterpolate = true, bool mustUpdateValueAtEveryNode = false);
	void setValueForRegion(uint32_t pos, uint32_t length, int32_t value, ModelStackWithAutoParam const* modelStack,
	                       ActionType actionType = ActionType::NOTE_EDIT);
	void setValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const* modelStack, int32_t pos,
	                               int32_t length, bool mayDeleteNodesInLinearRun = true);
	// Curve evaluation can operate without a playback/model context.
	int32_t getValueAtPos(uint32_t pos, int32_t loopLength, bool reversed = false);
	bool grabValueFromPos(uint32_t pos, int32_t loopLength);
	int32_t getValueAtPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed = false);
	/// tick the interolator by a number of samples - used for internal synths
	bool tickSamples(int32_t numSamples, bool use_float_interpolation);
	/// tick the interpolator by a number of ticks - used for midi
	bool tickTicks(int32_t numTicks, bool use_float_interpolation);
	void setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed);
	bool grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack);
	void generateRepeats(uint32_t oldLength, uint32_t newLength, bool shouldPingpong);
	void cloneFrom(AutoParam* otherParam, bool copyAutomation);
	Error beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength);
	void copyOverridingFrom(AutoParam* otherParam);
	void trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack);
	// Prepare an independent retained prefix before the original curve is handed to undo.
	// The destination must be empty and num_nodes must select an existing prefix of at least two nodes.
	Error copyNodesForTrim(int32_t num_nodes, LazyParamNodeVector& trimmed) const;
	void deleteAutomation(Action* action, ModelStackWithAutoParam const* modelStack, bool shouldNotify = true);
	void deleteAutomationBasicForSetup();
	void writeToFile(Serializer& writer, bool writeAutomation, const int32_t* valueForOverride = nullptr);
	Error readFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	bool containsSomething(uint32_t neutralValue = 0);
	static bool containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue = 0);
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
	/// @brief Make sure the value of this AutoParam will be \p value at time \p pos.
	///
	/// If no node exists at the provided position, a new node will be created.
	///
	/// @param[in] pos Position to modify.
	/// @param[in] value Value to use.
	/// @param[in] shouldInterpolate Sets \ref "ParamNode::interpolated" on the created or modified node.
	///
	/// @return the index in to \ref nodes of the modified node.
	int32_t setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate);
	int32_t homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos, int32_t length,
	                         int32_t startValue, bool interpolateLeftNode, bool interpolateRightNode,
	                         int32_t effectiveLength, bool reversed, int32_t posAtWhichClipWillCut = 2147483647);
	int32_t getDistanceToNextNode(ModelStackWithAutoParam const* modelStack, int32_t pos, bool reversed);
	void setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t value);

	[[nodiscard]] int32_t getCurrentValue() const {
		return (clipValue() != nullptr) && (nodes.get() != nullptr)
		           ? nodes.get()->value.getValueForBase(clipValue()->getValue())
		           : getClipValue();
	}
	// Owning collections already know the clip scalar. Avoid resolving its binding twice.
	[[nodiscard]] int32_t getValueForClip(int32_t clip_value) const {
		auto* curve = nodes.get();
		return curve != nullptr && curve->getNumElements() != 0 ? curve->value.getValueForBase(clip_value) : clip_value;
	}
	[[nodiscard]] int32_t getClipValue() const {
		return (clipValue() != nullptr) ? clipValue()->getValue() : standaloneValue();
	}
	[[nodiscard]] int64_t getAutomationOffset() const {
		return (clipValue() != nullptr) && (nodes.get() != nullptr) ? nodes.get()->value.getOffset() : 0;
	}
	void bindClipValue(ClipParamValue* clip);
	// Data-only undo/redo operation; the model-stack wrapper notifies consumers.
	[[nodiscard]] int64_t getUnclampedClipValue() const {
		return clipValue() != nullptr ? clipValue()->getUnclampedValue() : standaloneValue();
	}
	[[nodiscard]] int32_t getScalarForEffectiveValue(int32_t value) const;
	void captureState(AutoParamState& state, bool stealNodes);
	void swapStateBasicForSetup(AutoParamState* state);
	// Relocated scalar and curve already share a reference; never dereference the old scalar.
	void rebindClipValueAfterMove(ClipParamValue* clip) {
		// The node pointer tag selects the active scalar-binding union member.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		binding_.clip = clip;
		if (clip == nullptr) {
			// Activate the standalone member when clearing the binding.
			setStandaloneValue(0);
		}
		nodes.setHasClipValue(clip != nullptr);
	}
	int32_t getNodeValue(const ParamNode* node) const;
	void setNodeValue(ParamNode* node, int32_t value);
	[[nodiscard]] int64_t getCurveValue() const {
		return (clipValue() != nullptr) ? getAutomationOffset() : standaloneValue();
	}
	[[nodiscard]] int32_t getNodeBase() const { return (clipValue() != nullptr) ? clipValue()->getValue() : 0; }
	void restoreClipValueFromFile(int32_t value);
	void writeRelativeToFile(Serializer& writer) const;
	bool readRelativeFromFile(Deserializer& reader);
	void restoreClipValue(int32_t value);
	void clearAutomationOffset() const {
		if (nodes.get() != nullptr) {
			nodes.get()->value.setOffsetForBase(0, getNodeBase());
		}
	}
	int32_t getValuePossiblyAtPos(int32_t pos, ModelStackWithAutoParam* modelStack);
	void notifyPingpongOccurred(bool use_float_interpolation);
	void setCurrentValueBasicForSetup(int32_t value);
	[[nodiscard]] bool isAutomated() const { return nodes.getNumElements() != 0; }
	void cancelOverriding() { setOverridingTime(0); }
	// Existing public curve-editing interface; this object owns its nodes.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	LazyParamNodeVector nodes;
	bool hasInterpolationIncrement(bool use_float_interpolation);
	void resetInterpolationIncrement();

private:
	union Binding {
		int32_t standalone = 0;
		ClipParamValue* clip;
	} binding_;
	// These accessors select the member using nodes.hasClipValue().
	[[nodiscard]] int32_t standaloneValue() const {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		return binding_.standalone;
	}
	void setStandaloneValue(int32_t value) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		binding_.standalone = value;
	}
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	[[nodiscard]] ClipParamValue* clipValue() const { return nodes.hasClipValue() ? binding_.clip : nullptr; }
	[[nodiscard]] int32_t resolveOffset(int64_t offset) const {
		return ClipParamValue::resolve(int64_t{getNodeBase()} + offset);
	}
	void setCurveValue(int64_t offset) {
		if (clipValue() == nullptr) {
			setStandaloneValue(ClipParamValue::resolve(offset));
		}
		else if (nodes.get() != nullptr) {
			nodes.get()->value.setOffsetForBase(offset, getNodeBase());
		}
	}
	[[nodiscard]] uint32_t getOverridingTime() const {
		return (nodes.get() != nullptr) ? nodes.get()->renewedOverridingAtTime : 0;
	}
	void setOverridingTime(uint32_t time) const {
		if (nodes.get() != nullptr) {
			nodes.get()->renewedOverridingAtTime = time;
		}
	}
	void rebaseNodes(int64_t difference);
	int64_t getOffsetAtPos(uint32_t pos, int32_t loopLength, bool reversed = false);
	Error readAutomationFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	Error readLegacyAutomationEndpoint(int32_t value, bool interpolated);
	int64_t getOffsetAtPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed = false);
	bool deleteRedundantNodeInLinearRun(int32_t lastNodeInRunI, int32_t effectiveLength,
	                                    bool mayLoopAroundBackToEnd = true);
	void setupInterpolation(ModelStackWithAutoParam const* modelStack, ParamNode* nextNode, int32_t effectiveLength,
	                        int32_t currentPos, bool reversed);
	void homogenizeRegionTestSuccess(int32_t pos, int32_t regionEnd, int32_t startValue, bool interpolateStart,
	                                 bool interpolateEnd);
	void deleteNodesBeyondPos(int32_t pos);

	// interpolation to calculate current value
	bool useFloatInterpolation(ModelStackWithAutoParam const* modelStack);
	void calculateInterpolationIncrement(int64_t half_distance, int32_t ticks_til_next_node,
	                                     bool use_float_interpolation);
	void potentiallyOverrideInterpolationIncrement(int32_t limit, bool use_float_interpolation);
	void reverseInterpolationIncrement(bool use_float_interpolation);
	int64_t consumeFloatInterpolationIncrement(float value_increment);
	bool applyValueIncrement(int64_t value_increment);
};

// An idle parameter owns only its lazy curve and scalar binding.
static_assert(sizeof(AutoParam) == 2 * sizeof(void*));
