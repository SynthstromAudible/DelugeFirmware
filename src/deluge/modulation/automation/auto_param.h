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
#include "deluge-project/param_automation.h" // deluge::project::DocAutoParam (held by value)
#include "deluge-project/param_edit.h"       // deluge::project::RegionWriteResult (projectRegionFromStore)
#include "modulation/action/action_type.h"   // ActionType (used as a default-argument value below)
#include "modulation/params/param_node_vector.h"
#include <cstdint>

// model/action/action.h and storage/storage_manager.h are deliberately NOT included here: ActionType comes
// from the leaf modulation/action/action_type.h, while Action / Serializer / Deserializer appear only as
// method-parameter types below, so forward declarations suffice and the heavy includes live in
// auto_param.cpp. This keeps AutoParam (and the synth core, which reaches it via ParamSet) off model/ and
// storage/.
class InstrumentClip;
namespace deluge::project {
struct ParamNode;
}
using deluge::project::ParamNode;
class Action;
class Serializer;
class Deserializer;
class ParamCollection;
class Sound;
class CopiedParamAutomation;
class TimelineCounter;
class Clip;
class ModelStackWithAutoParam;

// For backing up a snapshot
class AutoParamState {
public:
	ParamNodeVector nodes{};
	int32_t value{};
};

struct StolenParamNodes;

class AutoParam {
public:
	AutoParam();
	// Copies carry the current value and reset the transient interpolation/override state; moves carry all of it.
	// No node storage is held here, so neither touches automation nodes.
	AutoParam(AutoParam const& other);
	AutoParam& operator=(AutoParam const& other);
	AutoParam(AutoParam&& other) noexcept;
	AutoParam& operator=(AutoParam&& other) noexcept;
	void init(deluge::project::DocAutoParam& doc);

	void setCurrentValueInResponseToUserInput(int32_t value, ModelStackWithAutoParam const* modelStack,
	                                          bool shouldLogAction = true, int32_t livePos = -1,
	                                          bool mayDeleteNodesInLinearRun = true, bool isMPE = false);
	/// @brief Model-first projection half of the scalar poke.
	///
	/// The persistent store `current_value` has already been written (ParamSnapshot::applyPatch); this derives the
	/// engine's RT state from it, reproducing setCurrentValueInResponseToUserInput's non-recording scalar tail
	/// (interpolation reset, the playing latch, cursor sync, notify) but sourcing the value from the store.
	///
	/// @param modelStack The param's model stack.
	/// @param oldValue   The value before the edit.
	/// @param isPlaying  Whether playback is running.
	void projectScalarFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool isPlaying);
	/// @brief Model-first projection half of the region poke, for every clock state.
	///
	/// The store (nodes and current_value) has already been written (ParamSnapshot::applyPatch's region kernel); this
	/// reproduces setValueForRegion's post-mutation tail: cursor sync, then the interpolation reset (clock active,
	/// playhead in region) or UI notify (clock active, playhead outside), then notify. The branch is the one
	/// `result.currentValueEffect` names; the kernel decides it and this tail replays it.
	///
	/// @param modelStack      The param's model stack.
	/// @param oldValue        The value before the edit.
	/// @param automatedBefore Whether the param was automated before the edit.
	/// @param result          The region kernel's result.
	void projectRegionFromStore(ModelStackWithAutoParam const* modelStack, int32_t oldValue, bool automatedBefore,
	                            deluge::project::RegionWriteResult result);
	int32_t processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed, bool didPinpong,
	                          bool mayInterpolate = true, bool mustUpdateValueAtEveryNode = false);
	void setValueForRegion(uint32_t pos, uint32_t length, int32_t value, ModelStackWithAutoParam const* modelStack,
	                       ActionType actionType = ActionType::NOTE_EDIT);
	void setValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const* modelStack, int32_t pos,
	                               int32_t length, bool mayDeleteNodesInLinearRun = true);
	int32_t getValueAtPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed = false);
	/// tick the interolator by a number of samples - used for internal synths
	bool tickSamples(int32_t numSamples);
	/// tick the interpolator by a number of ticks - used for midi
	bool tickTicks(int32_t numTicks);
	void setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed);
	bool grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack);
	void generateRepeats(deluge::project::DocAutoParam& doc, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	/// @brief Reverse this param's automation in place (used when flattening a reversed clip on clone).
	///
	/// Moves the nodes out and rebuilds them reversed.
	///
	/// @param doc                         The param's node store.
	/// @param reverseDirectionWithLength  The loop length to reverse within.
	/// @throws deluge::exception::BAD_ALLOC if the node buffer cannot be re-grown.
	void flattenReverse(deluge::project::DocAutoParam& doc, int32_t reverseDirectionWithLength);
	void copyOverridingFrom(AutoParam* otherParam);
	void trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack);
	void deleteAutomation(Action* action, ModelStackWithAutoParam const* modelStack, bool shouldNotify = true);
	void deleteAutomationBasicForSetup(deluge::project::DocAutoParam& doc);
	void writeToFile(deluge::project::DocAutoParam const& doc, Serializer& writer, bool writeAutomation,
	                 int32_t* valueForOverride = nullptr);
	Error readFromFile(deluge::project::DocAutoParam& doc, Deserializer& reader, int32_t readAutomationUpToPos);
	bool containsSomething(const deluge::project::DocAutoParam& doc, uint32_t neutralValue = 0);
	static bool containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue = 0);
	void shiftValues(deluge::project::DocAutoParam& doc, int32_t offset);
	void shiftHorizontally(deluge::project::DocAutoParam& doc, int32_t amount, int32_t effectiveLength);
	void swapState(AutoParamState* state, ModelStackWithAutoParam const* modelStack);
	void copy(int32_t startPos, int32_t endPos, CopiedParamAutomation* copiedParamAutomation, bool isPatchCable,
	          ModelStackWithAutoParam const* modelStack);
	void paste(int32_t startPos, int32_t endPos, float scaleFactor, ModelStackWithAutoParam const* modelStack,
	           CopiedParamAutomation* copiedParamAutomation, bool isPatchCable);
	Error makeInterpolationGoodAgain(deluge::project::DocAutoParam& doc, int32_t clipLength,
	                                 int32_t quantizationRShift);
	void transposeCCValuesToChannelPressureValues(deluge::project::DocAutoParam& doc);
	void deleteTime(int32_t startPos, int32_t lengthToDelete, ModelStackWithAutoParam* modelStack);
	void insertTime(deluge::project::DocAutoParam& doc, int32_t pos, int32_t lengthToInsert);
	void appendParam(deluge::project::DocAutoParam& doc, deluge::project::DocAutoParam const& srcDoc, int32_t oldLength,
	                 int32_t reverseThisRepeatWithLength, bool pingpongingGenerally);
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
	int32_t setNodeAtPos(deluge::project::DocAutoParam& doc, int32_t pos, int32_t value, bool shouldInterpolate);
	int32_t homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos, int32_t length,
	                         int32_t startValue, bool interpolateLeftNode, bool interpolateRightNode,
	                         int32_t effectiveLength, bool reversed, int32_t posAtWhichClipWillCut = 2147483647);
	int32_t getDistanceToNextNode(ModelStackWithAutoParam const* modelStack, int32_t pos, bool reversed);
	void setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t value);

	inline int32_t getCurrentValue() { return currentValue; }
	int32_t getValuePossiblyAtPos(int32_t pos, ModelStackWithAutoParam* modelStack);
	void notifyPingpongOccurred();

	/// Engine-only setter for runtime re-setters (transient FX like stutter/slice): the store holds the pre-FX base
	/// value, not the live FX value.
	inline void setCurrentValueBasicForSetup(int32_t value) { currentValue = value; }

	/// Default/setup setter: write the persisted scalar into the owner's store @p doc and the RT cursor from the same
	/// literal. Used by initParams so param defaults live in the DOM store directly.
	/// @param doc   The owner-resolved store slot, e.g. `set->docForParam(p)`.
	/// @param value The value to store.
	inline void setCurrentValueBasicForSetup(deluge::project::DocAutoParam& doc, int32_t value) {
		currentValue = value;
		doc.current_value = value;
	}

	inline bool isAutomated(const deluge::project::DocAutoParam& doc) { return std::ssize(doc.nodes) != 0; }

	inline void cancelOverriding() { // Will also cancel "latching".
		renewedOverridingAtTime = 0;
	}

	// The AutoParam is a pure interpolation cursor: it holds no node storage. Every node operation receives a
	// `DocAutoParam&` resolved by the owner (ModelStackWithAutoParam::doc(), or a per-owner docForParam). The DOM owns
	// all automation node data. If a param has no nodes, currentValue is used.

	/// Current value of the AutoParam. Updated by several functions.
	int32_t currentValue;

	// interpolation to calculate current value
	bool hasInterpolationIncrement();
	void resetInterpolationIncrement();
	int32_t valueIncrementPerHalfTick;
	float value_increment_per_half_tick_float;
	float interpolation_increment_remainder_float;

	uint32_t renewedOverridingAtTime; // If 0, it's off. If 1, it's latched until we hit some nodes / automation

	// "Latching" happens when you start recording values, but then stops if you arrive at any pre-existing values. So
	// it only works in empty stretches of time.

private:
	bool deleteRedundantNodeInLinearRun(deluge::project::DocAutoParam& doc, int32_t lastNodeInRunI,
	                                    int32_t effectiveLength, bool mayLoopAroundBackToEnd = true);
	void setupInterpolation(ModelStackWithAutoParam const* modelStack, ParamNode* nextNode, int32_t effectiveLength,
	                        int32_t currentPos, bool reversed);

	// interpolation to calculate current value
	bool useFloatInterpolation(ModelStackWithAutoParam const* modelStack);
	void calculateInterpolationIncrement(int32_t half_distance, int32_t ticks_til_next_node,
	                                     bool use_float_interpolation);
	void potentiallyOverrideInterpolationIncrement(int32_t limit, bool use_float_interpolation);
	void reverseInterpolationIncrement();
	int32_t consumeFloatInterpolationIncrement(float value_increment);
	bool applyValueIncrement(int32_t value_increment);
};
