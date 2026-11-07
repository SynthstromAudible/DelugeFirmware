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

#include "deluge-project/lane_view.h"
#include "foundation/function_ref.h"
#include "modulation/params/param.h"

#include <atomic>
#include <cstdint>

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
namespace deluge::modulation {
struct LaneSnapshot;
class LanePublication;
class WorkingCopies;
} // namespace deluge::modulation

class ParamCollection {
public:
	ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary);
	/// @brief A copy of @p other's lanes' owner, with no snapshot of its own yet: the next walk of the song that plays
	/// it
	///        publishes one.
	ParamCollection(const ParamCollection& other);
	ParamCollection& operator=(const ParamCollection&) = delete;
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

	/// @brief [interaction] Call @p visit with each of this collection's lanes that has nodes, by its paramId (the key
	///        its published snapshot is searched by), in any order.
	virtual void forEachLane(deluge::function_ref<void(int32_t, const deluge::project::DocAutoParam&)> visit) {}

	/// @brief [render] or [interaction] Call @p visit with the key of each lane of this collection that a repeat of
	///        @p other, a collection of the same kind, appends to: each lane @p other has to repeat. Reads no lane.
	/// @param otherSummary @p other's summary: its flags say which of its lanes a ParamSet or PatchCableSet repeats.
	virtual void forEachAppendedKey(ParamCollection& other, const ParamCollectionSummary& otherSummary,
	                                deluge::function_ref<void(int32_t)> visit) {}
	/// @brief [interaction] Lane @p paramId of this collection to append a repeat to, made where the collection makes
	///        its lanes as they are first written.
	/// @return It, or nullptr when this collection has no such lane.
	virtual deluge::project::DocAutoParam* laneToAppendTo(int32_t paramId) { return findDocParamForId(paramId); }

	/// @brief [interaction] Call @p visit with each of this collection's lanes as a repeat of @p other appended at
	///        @p oldLength would leave them, changing none: a lane the repeat changes as a copy with the repeat
	///        appended, the rest as they are, each with nodes, by the key forEachLane() gives it.
	void forEachLaneAppended(ParamCollection& other, const ParamCollectionSummary& otherSummary, int32_t oldLength,
	                         int32_t reverseThisRepeatWithLength, bool pingpongingGenerally,
	                         deluge::function_ref<void(int32_t, const deluge::project::DocAutoParam&)> visit);

	/// @brief [render] or [interaction] Lane @p paramId as the render plays it: the published lane where
	///        deluge::modulation::snapshotToPlay() gives a snapshot, else the model's. A lane with no nodes is an empty
	///        view with base 0; every reader of one stops there.
	deluge::project::LaneView playedLane(int32_t paramId);

	/// @brief [render] or [interaction] The snapshot the render plays this collection's lanes from; nullptr before its
	///        first publish.
	[[nodiscard]] const deluge::modulation::LaneSnapshot* published() const {
		return published_.load(std::memory_order_acquire);
	}

	/// @brief [render] or [interaction] How many snapshots this collection has published: a change says its published
	///        lanes changed.
	[[nodiscard]] uint32_t publications() const { return publications_.load(std::memory_order_acquire); }

	/// @brief Which collection this is, of all made since boot: a request made on the render names it by this and its
	///        address, so a collection made later at the same address is not taken for it.
	[[nodiscard]] uint32_t serial() const { return serial_; }

	/// @brief [render] or [interaction] Whether any of this collection's lanes is being recorded into a working copy
	///        (modulation/automation/working_copies.h).
	[[nodiscard]] bool hasWorkingCopies() const { return workingCopies_ != 0; }

	const int32_t objectSize;
	int32_t ticksTilNextEvent{};

private:
	friend class deluge::modulation::LanePublication;
	friend class deluge::modulation::WorkingCopies;
	/// [interaction] -> [render] Swapped only with the render held off, or by the render itself at a staged swap; read
	/// by the render once per collection event.
	std::atomic<const deluge::modulation::LaneSnapshot*> published_{nullptr};
	/// [interaction] -> [render] The snapshot the render publishes itself at an arrangement take's loop end: its lanes
	/// with the next repeat appended, built and set on the interaction tier, taken by the render. It shares lanes with
	/// published_.
	std::atomic<const deluge::modulation::LaneSnapshot*> staged_{nullptr};
	/// [render] -> [interaction] The snapshot the render's last swap replaced, for the interaction tier to retire. It
	/// shares lanes with published_.
	std::atomic<const deluge::modulation::LaneSnapshot*> superseded_{nullptr};
	/// [interaction] The source collection's publications() when staged_ was built: a staged repeat built from other
	/// source lanes is built again.
	uint32_t stagedAgainst_ = 0;
	/// [interaction] or [render] Counted at each snapshot published or swapped in: unlike the snapshot's address, never
	/// the same for two snapshots.
	std::atomic<uint32_t> publications_{0};
	/// [interaction] Marked changed since the job last settled: the next walk of the song republishes it.
	bool lanesDirty_ = false;
	uint32_t serial_; ///< see serial()
	/// [interaction] -> [render] The working copies of this collection's lanes, changed with the render held off.
	uint8_t workingCopies_ = 0;
};
