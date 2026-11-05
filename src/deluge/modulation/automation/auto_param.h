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
class Song;
namespace deluge::modulation {
struct RecordEvent;
void applyRecordEvents(::Song& song);
} // namespace deluge::modulation

// For backing up a snapshot
class AutoParamState {
public:
	ParamNodeVector nodes{};
	int32_t value{}; ///< the played value
	int32_t base{};  ///< the lane's base (DocAutoParam::current_value)
};

struct StolenParamNodes;

/// @brief Whether @p lane is the stand-in the render reads for a lane nothing holds yet: it reads as unautomated, and
/// nothing may be written into it.
[[nodiscard]] bool isUnheldLane(const deluge::project::DocAutoParam& lane);
/// @brief The lane the render reads for a param nothing holds: no nodes, base 0.
[[nodiscard]] const deluge::project::DocAutoParam& unheldLane();

#if ALPHA_OR_BETA_VERSION
namespace deluge::modulation {
/// Writes found in the unheld lane when the render next reached it (each undone): it must stay 0.
extern uint32_t unheldLaneWrites;
/// A lane cursor moved while playing by the interaction tier with the render not excluded: it must stay 0.
extern uint32_t strayRenderCodeCalls;
} // namespace deluge::modulation
#endif

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

	/// @brief The value the param plays now: its lane at the playhead, else its base, as the cursor last left it.
	[[nodiscard]] int32_t played() const { return played_; }
	inline int32_t getCurrentValue() const { return played_; }
	int32_t getValuePossiblyAtPos(int32_t pos, ModelStackWithAutoParam* modelStack);
	void notifyPingpongOccurred();

	/// @brief Write @p lane's base (the model's value) and play it: edits, file reads, setup defaults.
	void setBase(deluge::project::DocAutoParam& lane, int32_t value) {
		deluge::project::set_base(lane, value);
		played_ = value;
	}
	/// @brief Play @p lane's base: the cursor projected from the model after a read or a rebuild.
	void playBase(const deluge::project::DocAutoParam& lane) { played_ = lane.current_value; }
	/// @brief The legacy instrument's rule before a value-touching kernel: the played value becomes the base.
	void adoptPlayedAsBase(deluge::project::DocAutoParam& lane) const { deluge::project::set_base(lane, played_); }
	/// @brief Play @p value without touching the base: a transient effect (stutter, a sample preview's adopted
	/// volume), or what a lane played carried across a rebuild of its owner.
	void setPlayed(int32_t value) { played_ = value; }

	inline bool isAutomated(const deluge::project::DocAutoParam& doc) { return std::ssize(doc.nodes) != 0; }

	inline void cancelOverriding() { // Will also cancel "latching".
		renewedOverridingAtTime = 0;
	}

	// The AutoParam is a pure interpolation cursor: it holds no node storage. Every node operation receives a
	// `DocAutoParam&` resolved by the owner (ModelStackWithAutoParam::doc(), or a per-owner docForParam). The DOM owns
	// all automation node data. If a param has no nodes, its base value is played.

private:
	/// The played value: written by the executor (playback, interpolation, the latch), by projections from the model
	/// (setBase, playBase) and by setPlayed; the base lives in the lane's `current_value`.
	int32_t played_;

public:
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
	friend void deluge::modulation::applyRecordEvents(::Song& song);

	/// @brief Write a value recorded while playing into @p lane: the live-record kernel, with what it decides from the
	/// lane's own nodes (whether the region's start interpolates, which node of a straight run to drop). The render's
	/// apply of a queued event; the interaction tier runs it only with the render excluded.
	void writeRecordedValue(deluge::project::DocAutoParam& lane, const deluge::modulation::RecordEvent& event);
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
