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
#include "deluge-project/param_automation.h"
#include "instruments/deluge/cable_records.h"
#include "modulation/params/param_collection.h"
#include "modulation/patch/patch_cable.h"
#include "synth/matrix/program.h"
#include "synth/modulation/patch_routing.h"
#include <bitset>
#include <memory>
#include <span>
#include <vector>

class Song;
class ModelStackWithParamCollection;
class ModelStackWithThreeMainThings;
class Sound;
class ParamManager;
class LearnedMIDI;
class MIDICable;

struct CableGroup {
	uint8_t first;
	uint8_t end;
};

/// @brief A cable and its depth's lane, as an undo or redo puts it back.
struct CableDepth {
	deluge::synth::CableRecord cable;
	deluge::project::DocAutoParam lane;
};

class PatchCableSet final : public ParamCollection {
public:
	PatchCableSet(ParamCollectionSummary* summary);
	/// @brief Deep-copy a set: real-copies each usable cable's automation and the destination arrays (see clone()).
	/// @param other The set to copy.
	PatchCableSet(PatchCableSet const& other);
	~PatchCableSet() override;

	[[nodiscard]] bool domStoreIsExternal() const override { return externalDepths_ != nullptr; }

	/// @brief The lane of cable @p c's depth: the owner's table once the set's lanes are relocated there, else the
	/// set's own.
	deluge::project::DocAutoParam* docForParam(int32_t c);
	/// @brief Move the set's own depth lanes into @p slot, the owner's table, and resolve from it from now on.
	void relocateDepthsInto(std::vector<deluge::project::DocCableDepth>& slot);
	[[nodiscard]] bool depthsRelocated() const { return externalDepths_ != nullptr; }
	/// @brief The lanes of the cables' depths, keyed by depth id.
	[[nodiscard]] const std::vector<deluge::project::DocCableDepth>& depthStore() const {
		return externalDepths_ != nullptr ? *externalDepths_ : ownDepths_;
	}
	deluge::project::DocAutoParam* docParamForId(int32_t paramId) override;
	/// @copydoc ParamCollection::findDocParamForId
	deluge::project::DocAutoParam* findDocParamForId(int32_t paramId) override;

	/// @brief Compile the records into the routing the render walks, reordering the records (and the cables'
	/// automation cursors) into the compiled order.
	/// @param sound        The sound whose sources and params decide which cables may be patched.
	/// @param paramManager The sound's params, which that decision reads.
	void setupPatching(Sound& sound, ParamManager& paramManager);
	/// @brief The compile with each cable's usability given.
	/// @param summary The set's summary, whose automation and interpolation records are refreshed.
	/// @param usable  Bit c: whether cable c (in the current order) may be patched.
	void setupPatching(ParamCollectionSummary& summary, std::bitset<kMaxNumPatchCables> usable);
	/// @brief The clip's cables: its patching's topology, in the order setupPatching() leaves them.
	[[nodiscard]] const deluge::instrument::CableRecords& records() const { return records_; }
	/// @brief Append a cable from @p record, its depth 0. It joins the unusable end of the list, where the render
	/// doesn't reach it, until setupPatching() places it.
	/// @return The new cable; nullptr at kMaxNumPatchCables cables, or if out of memory.
	PatchCable* appendCable(const deluge::synth::CableRecord& record);
	/// @brief Append a cable from @p from to param @p to with the source's default polarity and depth @p amount.
	/// @param amount The cable's depth, Q1.30.
	/// @return The new cable; nullptr at kMaxNumPatchCables cables, or if out of memory.
	PatchCable* appendDefaultCable(PatchSource from, uint8_t to, int32_t amount);
	/// @brief Take @p records as the clip's cables, as an undo or redo of a topology edit puts them: each cable keeps
	/// its depth (its lane and its cursor's value) by its source and target, a cable new to the set starts at depth 0,
	/// and a cable no longer present loses its lane. Recompiles.
	/// @param restore A cable's depth to put back with it (an undo or redo bringing a cable back), or null.
	/// @return False when a cable could not be added for want of memory: the set holds the cables before it.
	bool replaceRecords(Sound& sound, ParamManager& paramManager, const deluge::instrument::CableRecords& records,
	                    const CableDepth* restore = nullptr);
	/// @brief Move cable @p c to @p target, its depth with it. The caller recompiles.
	void retarget(uint8_t c, deluge::synth::CableTarget target);
	/// @brief Set cable @p c's polarity.
	void setPolarity(uint8_t c, Polarity polarity);
	bool doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor);
	/// @brief The index of the cable from @p from to @p destinationParamDescriptor, usable or not.
	/// @return The index, or 255 when there is none.
	uint8_t getPatchCableIndex(PatchSource from, ParamDescriptor destinationParamDescriptor);
	/// @brief Delete cable @p c and its automation, then re-place the rest.
	void deletePatchCable(Sound& sound, ParamManager& paramManager, uint8_t c);
	void removeAllPatchingToParam(ModelStackWithParamCollection* modelStack, uint8_t p);
	bool isSourcePatchedToSomething(PatchSource s);
	bool isSourcePatchedToSomethingManuallyCheckCables(PatchSource s);
	bool doesParamHaveSomethingPatchedToIt(int32_t p);

	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) override;
	void tickTicks(int32_t numSamples, ModelStackWithParamCollection* modelStack) override {};
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) override;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) override;
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) override;
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong) override;
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) override;
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching) override;
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength) override;
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
	                       bool didPingpong, bool mayInterpolate) override;
	Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
	void stripAutomation() override;
	void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
	ParamManagerForTimeline* getParamManager();

	void writePatchCablesToFile(Serializer& writer, bool writeAutomation);
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) override;

	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) override;
	/// @brief The depth of the cable @p modelStack names. With @p allowCreation (the caller writes it) a missing cable
	/// is added through the mod matrix and recorded; otherwise a missing cable gives a null autoParam.
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = false) override;
	static int32_t getParamId(ParamDescriptor destinationParamDescriptor, PatchSource s);

	AutoParam* getParam(int32_t paramId);

	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) override;

	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	bool isSourcePatchedToDestinationDescriptorVolumeInspecific(PatchSource s,
	                                                            ParamDescriptor destinationParamDescriptor);
	bool isAnySourcePatchedToParamVolumeInspecific(ParamDescriptor destinationParamDescriptor);
	void grabVelocityToLevelFromMIDIInput(LearnedMIDI* midiInput);
	void grabVelocityToLevelFromMIDICable(MIDICable& cable);
	PatchCable* getPatchCableFromVelocityToLevel();

	deluge::synth::RoutedDestination* getDestinationForParam(int32_t p);
	/// @brief The compiled routing, as the matrix compiler reads it.
	[[nodiscard]] deluge::synth::PatchRouting routing() const {
		return {{destinations[0], destinations[1]},
		        {sourcesPatchedToAnything[0], sourcesPatchedToAnything[1]},
		        routedCables_};
	}

	/// @brief The cables compiled for the matrix evaluator; null when the compile found no storage.
	[[nodiscard]] const deluge::synth::matrix::MatrixProgram* program() const { return program_; }

	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::PATCH_CABLE; }

	uint32_t sourcesPatchedToAnything[2]{}; // Only valid after setupPatching()

	/// @brief The cables, numPatchCables of them, in storage sized to fit; null when there are none.
	///
	/// In the order setupPatching() leaves them: the usable ones first, and each Destination's cables the run
	/// [firstCable, endCable). The render walks and ticks them, so the storage changes only in appendCable() and
	/// truncateCables(), which publish with the render excluded.
	PatchCable* patchCables{};
	uint8_t numUsablePatchCables{};
	/// How many cables there are. Changed only through appendCable() and truncateCables().
	uint8_t numPatchCables{};

	deluge::synth::RoutedDestination* destinations[2]{};
	/// One per cable, as the compile routed it: its source, polarity and range link. Fitted to the cables, freed with
	/// the Destinations.
	deluge::synth::RoutedCable* routedCables_{};
	/// The matrix program compiled with the routing, in a block sized to fit it; freed with the Destinations.
	const deluge::synth::matrix::MatrixProgram* program_{};

	/// @brief Drop every cable from index @p count on, shrinking the storage to fit the rest.
	///
	/// Dropping a usable cable also drops the Destinations, which may refer to it, so the render sees no patching
	/// until the caller re-runs setupPatching() - which it must anyway, and which also clears the summary's
	/// automation flags for the dropped indices.
	/// @param count At most numPatchCables.
	void truncateCables(uint8_t count);

	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override { return true; };

	static void dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, PatchSource* s);

private:
	/// @brief Append a cable, growing the storage to fit it.
	/// @return The new cable, default-initialised; nullptr at kMaxNumPatchCables cables, or if out of memory.
	PatchCable* addCable();
	/// @brief Reorder the records and the cables together: position c takes the one previously at @p order[c].
	void permuteCables(std::span<const uint8_t> order);

	deluge::instrument::CableRecords records_;

	std::vector<deluge::project::DocCableDepth> ownDepths_;
	std::vector<deluge::project::DocCableDepth>* externalDepths_ = nullptr;
	std::vector<deluge::project::DocCableDepth>& depths() {
		return externalDepths_ != nullptr ? *externalDepths_ : ownDepths_;
	}
	/// @brief The lane of the depth of the cable from @p from to @p target, created empty if there is none.
	deluge::project::DocAutoParam& laneFor(PatchSource from, ParamDescriptor target);
	void eraseLane(PatchSource from, ParamDescriptor target);

	/// Move the cables into storage for exactly @p count of them, default-initialising any new ones and destroying
	/// any dropped. @return false, with nothing changed, if the storage couldn't be allocated.
	bool resizeCables(uint8_t count);
	void swapCables(int32_t c1, int32_t c2);
	void freeDestinationMemory(bool destructing);
	/// @brief Deep-copy a sentinel-terminated Destination array (terminated by an entry with sources == 0).
	/// @param src The array to copy; may be nullptr.
	/// @return The copy, or nullptr if @p src is nullptr.
	/// @note Throws deluge::exception::BAD_ALLOC on OOM.
	static deluge::synth::RoutedDestination* cloneDestinations(deluge::synth::RoutedDestination const* src);
};

/// @brief The record of a runtime cable's topology.
[[nodiscard]] inline deluge::synth::CableRecord recordOf(const PatchCable& cable) {
	return {
	    cable.from, {cable.destinationParamDescriptor.data}, static_cast<deluge::synth::CablePolarity>(cable.polarity)};
}
static_assert(std::to_underlying(Polarity::UNIPOLAR) == std::to_underlying(deluge::synth::CablePolarity::UNIPOLAR));
static_assert(std::to_underlying(Polarity::BIPOLAR) == std::to_underlying(deluge::synth::CablePolarity::BIPOLAR));
