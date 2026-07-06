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
#include "model/action/action.h"
#include "modulation/params/param_node_vector.h"
#include "storage/storage_manager.h"
#include <cstdint>

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
	ParamNodeVector nodes{};
	int32_t value{};
};

struct StolenParamNodes;

class AutoParam {
public:
	AutoParam();
	// Deep copy with clone semantics: deep-copies the (borrow-aware) nodes and resets the transient
	// interpolation/override state. The store is reached via a redirectable `nodes_` pointer (default
	// `&ownNodes_`, or a DOM-owned store when borrowed). Because `nodes_` is a SELF-pointer, all of copy/move
	// must re-point it — a defaulted move would leave a moved-into param's `nodes_` pointing at the *source's*
	// `ownNodes_` (a dangling read; MIDIParamVector growth move-constructs these). Copies own their storage
	// (`nodes_ = &ownNodes_`); moves inherit an external borrow but re-anchor a self-borrow.
	AutoParam(AutoParam const& other);
	AutoParam& operator=(AutoParam const& other);
	AutoParam(AutoParam&& other) noexcept;
	AutoParam& operator=(AutoParam&& other) noexcept;
	void init(deluge::project::DocAutoParam& doc);

	void setCurrentValueInResponseToUserInput(int32_t value, ModelStackWithAutoParam const* modelStack,
	                                          bool shouldLogAction = true, int32_t livePos = -1,
	                                          bool mayDeleteNodesInLinearRun = true, bool isMPE = false);
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
	/// Reverse this param's automation in place (used when flattening a reversed clip on clone). Operates on
	/// already-owned nodes — moves them out, rebuilds reversed. reverseDirectionWithLength is the loop length.
	/// Throws deluge::exception::BAD_ALLOC if it can't re-grow the node buffer.
	void flattenReverse(deluge::project::DocAutoParam& doc, int32_t reverseDirectionWithLength);
	void copyOverridingFrom(AutoParam* otherParam);
	void trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack);
	void deleteAutomation(Action* action, ModelStackWithAutoParam const* modelStack, bool shouldNotify = true);
	void deleteAutomationBasicForSetup(deluge::project::DocAutoParam& doc);
	void writeToFile(deluge::project::DocAutoParam const& doc, Serializer& writer, bool writeAutomation,
	                 int32_t* valueForOverride = nullptr);
	Error readFromFile(deluge::project::DocAutoParam& doc, Deserializer& reader, int32_t readAutomationUpToPos);
	bool containsSomething(uint32_t neutralValue = 0);
	static bool containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue = 0);
	void shiftValues(deluge::project::DocAutoParam& doc, int32_t offset);
	void shiftParamVolumeByDB(deluge::project::DocAutoParam& doc, float offset);
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

	inline void setCurrentValueBasicForSetup(int32_t value) { currentValue = value; }

	inline bool isAutomated() { return (std::ssize(nodes())); }

	inline void cancelOverriding() { // Will also cancel "latching".
		renewedOverridingAtTime = 0;
	}

	/// The nodes that make up this parameter (if empty, \ref currentValue applies). The accessor is a clean seam
	/// (the projection layer + the ~130 op call sites go through it); the nodes live in a `DocAutoParam` reached
	/// via a redirectable handle (`&ownDoc_` normally, or a DOM-owned `DocAutoParam` when borrowed).
	ParamNodeVector& nodes() { return doc_->nodes; }
	const ParamNodeVector& nodes() const { return doc_->nodes; }

	/// M2-E (E1, transitional): the handle the ModelStack threads to this param's ops. Currently returns the
	/// stored `doc_` (so `modelStack->doc() == this->doc_` for a param's own ops — behaviour-identical); in M2-E3
	/// the ModelStack resolves the handle from the owner instead and this accessor + `doc_` are removed.
	deluge::project::DocAutoParam* docForThreading() { return doc_; }

	/// Point this param at a DOM-owned `DocAutoParam` and move its current nodes into it. The store must outlive
	/// this param and never relocate (the DOM holds it at a stable address). Afterwards, nodes() is the DOM
	/// store's node vector and all engine edits/undo operate on it directly. (M2-B: the handle is now the whole
	/// `DocAutoParam`, not just its `.nodes` — scaffolding for forwarding ops to param_edit kernels; `doc_` is
	/// removed entirely in M2-E, when callers thread the `DocAutoParam` per-call instead of it being stored.)
	void borrowNodeStorage(deluge::project::DocAutoParam* domDoc);

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
	/// This param's default `DocAutoParam` storage (used when `doc_` points here, i.e. un-borrowed). Owned by
	/// value. Only its `.nodes` is engine-relevant in M2-B; `.current_value` (the DOM base) is left untouched —
	/// the RT cursor lives in the separate `currentValue` member.
	deluge::project::DocAutoParam ownDoc_{};
	/// Redirectable borrow-handle to the active `DocAutoParam`: `&ownDoc_` normally, or a DOM-owned store after
	/// borrowNodeStorage(). Trivially relocatable, but a self-pointer — copy/move re-point it (see the special
	/// members). All node access goes through nodes() (== doc_->nodes). Scaffolding: removed in M2-E.
	deluge::project::DocAutoParam* doc_ = &ownDoc_;

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
