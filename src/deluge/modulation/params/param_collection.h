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
namespace deluge::project {
struct ParamNode;
}
using deluge::project::ParamNode;
class AutoParam;
class CopiedParamAutomation;
class TimelineCounter;
class Output;
class Clip;
class ModelStackWithAutoParam;
class ModelStackWithParamId;
class ModelStackWithParamCollection;
class ParamCollectionSummary;
namespace deluge::project {
struct DocAutoParam;
}

class ParamCollection {
public:
	ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary);
	virtual ~ParamCollection();

	/// @brief Resolve the DOM node-store handle for the param identified by @p paramId (as carried on a
	///        ModelStackWithAutoParam).
	///
	/// Each concrete collection maps its own paramId to its store slot.
	///
	/// @param paramId The param id within this collection.
	/// @return The param's node store; nullptr by default.
	virtual deluge::project::DocAutoParam* docParamForId(int32_t paramId) { return nullptr; }

	/// @brief The node store docParamForId() would return, if the collection already holds one: it creates nothing.
	///
	/// The render's lookup (ModelStackWithAutoParam::doc()): a store the render grew could move under another tier
	/// reading it.
	/// @param paramId The param id within this collection.
	/// @return The param's node store, or nullptr when it has none yet.
	virtual deluge::project::DocAutoParam* findDocParamForId(int32_t paramId) { return nullptr; }

	/// @brief Whether this collection resolves params against an external (project-tree) DOM store rather than its
	///        own engine-inline storage.
	///
	/// When true, reaching docParamForId is legitimate lazy create-on-demand into the tree (a MIDI cc / patch cable
	/// not yet mirrored lands in the bound tree vector), not an engine fallback. The doc() totality gate counts only
	/// the false case (an owner still on engine-inline storage).
	///
	/// @return True if bound to an external store; false by default.
	[[nodiscard]] virtual bool domStoreIsExternal() const { return false; }

	/// @brief Deep-clone this collection into freshly-allocated memory (fallible).
	///
	/// Each concrete collection copy-constructs itself; @p copyAutomation false drops automation
	/// (stripAutomation) and a nonzero @p reverseDirectionWithLength flatten-reverses it (flattenReverseAutomation).
	///
	/// @param result                     Set to the clone on success.
	/// @param copyAutomation             Whether to keep the automation.
	/// @param reverseDirectionWithLength Loop length to flatten-reverse within; 0 for no reversal.
	/// @return Error::NONE on success, else the failure (e.g. Error::INSUFFICIENT_RAM).
	virtual Error clone(ParamCollection*& result, bool copyAutomation,
	                    int32_t reverseDirectionWithLength = 0) const = 0;

	/// @brief Empty every owned automation node vector (for a "copy structure, drop automation" clone). Default: no-op.
	virtual void stripAutomation() {}
	/// @brief Reverse every owned param's automation in place (flatten-reversing on clone). Default: no-op.
	/// @param reverseDirectionWithLength The loop length to reverse within.
	virtual void flattenReverseAutomation(int32_t reverseDirectionWithLength) {}

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
	virtual bool shouldInterpolateWithFloat(ModelStackWithParamId const* modelStack) { return false; }
	virtual bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) { return false; }
	virtual bool shouldRecordUnautomatedParamChange(ModelStackWithParamId const* modelStack) { return true; }
	virtual int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	virtual int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack);
	virtual void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack);
	virtual deluge::modulation::params::Kind getParamKind() = 0;

	const int32_t objectSize;
	int32_t ticksTilNextEvent{};
};
