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
#include "modulation/params/param_collection.h"
#include "modulation/patch/patch_cable.h"
#include <memory>
#include <vector>

namespace deluge::project {
struct DocParamSet;
struct DocCable;
struct DocAutoParam;
} // namespace deluge::project

class Song;
class ModelStackWithParamCollection;
class ModelStackWithThreeMainThings;
class LearnedMIDI;
class MIDICable;

struct CableGroup {
	uint8_t first;
	uint8_t end;
};

struct Destination {
	ParamDescriptor destinationParamDescriptor;
	uint32_t sources;
	uint8_t firstCable;
	uint8_t endCable;
};

class PatchCableSet final : public ParamCollection {
public:
	PatchCableSet(ParamCollectionSummary* summary);
	/// @brief Deep-copy a set: real-copies each usable cable's automation and the destination arrays (see clone()).
	/// @param other The set to copy.
	PatchCableSet(PatchCableSet const& other);
	~PatchCableSet() override;

	/// @brief No-op: each cable's nodes live inline in its amount_ (see PatchCable::amount_), because cables are
	///        reordered by index at runtime and a positional shared store could not stay in sync.
	// TODO: bindDomStore() does nothing and domStore() always returns nullptr; delete both and their callers.
	void bindDomStore();
	/// @brief The bound DOM store. Test / inspection accessor; the set is never bound (see bindDomStore).
	/// @return Always nullptr.
	deluge::project::DocParamSet* domStore();

	/// @brief Bind this set's cable amount-automation to an external, identity-keyed persistent store (the
	///        owner's project-tree `DocParamManager::cables`).
	///
	/// Once bound, docForParam resolves a cable's nodes from the tree entry matching its (from, destination)
	/// (save and runtime edits share it) instead of the cable's inline `amount_`. Cables are matched by
	/// identity, not index, so runtime reorder (setupPatching) is transparent.
	/// @param ext The external cable store; non-owning.
	void bindDomCablesExternal(std::vector<deluge::project::DocCable>* ext) { domCablesRef_ = ext; }
	/// @brief The bound external cable store, or nullptr if unbound. Test / inspection accessor.
	/// @return The bound store, or nullptr.
	std::vector<deluge::project::DocCable>* domCables() { return domCablesRef_; }
	[[nodiscard]] bool domStoreIsExternal() const override { return domCablesRef_ != nullptr; }

	deluge::project::DocAutoParam* docForParam(int32_t i);

	/// @brief Set cable @p i's polarity: the cable's, and its record's when the set is bound, which a save writes.
	/// @param i        The cable's index.
	/// @param polarity The new polarity.
	void setPolarity(int32_t i, Polarity polarity);
	/// @brief Cable @p i's polarity as its record holds it when the set is bound, else the cable's.
	/// @param i The cable's index.
	/// @return The polarity.
	[[nodiscard]] Polarity recordedPolarity(int32_t i);
	deluge::project::DocAutoParam* docParamForId(int32_t paramId) override;

	void setupPatching(ModelStackWithParamCollection const* modelStack);
	bool doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor);
	uint8_t getPatchCableIndex(PatchSource from, ParamDescriptor destinationParamDescriptor,
	                           ModelStackWithParamCollection const* modelStack = nullptr,
	                           bool createIfNotFound = false);
	void deletePatchCable(ModelStackWithParamCollection const* modelStack, uint8_t c);
	bool patchCableIsUsable(uint8_t c, ModelStackWithThreeMainThings const* modelStack);
	int32_t getModifiedPatchCableAmount(int32_t c, int32_t p);
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
	AutoParam* getParam(ModelStackWithParamCollection const* modelStack, PatchSource s,
	                    ParamDescriptor destinationParamDescriptor, bool allowCreation = false);
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

	Destination* getDestinationForParam(int32_t p);

	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::PATCH_CABLE; }

	uint32_t sourcesPatchedToAnything[2]{}; // Only valid after setupPatching()

	/// @brief The cables, numPatchCables of them, in storage sized to fit; null when there are none.
	///
	/// In the order setupPatching() leaves them: the usable ones first, and each Destination's cables the run
	/// [firstCable, endCable). The render walks and ticks them, so the storage changes only in addCable() and
	/// truncateCables(), which publish with the render excluded.
	PatchCable* patchCables{};
	uint8_t numUsablePatchCables{};
	/// How many cables there are. Changed only through addCable() and truncateCables().
	uint8_t numPatchCables{};

	/// DOM-owned node store the cable amount-AutoParams borrow (nullptr = unbound). RAII-freed with this
	/// (heap-allocated, stable-address) collection. See bindDomStore().
	std::unique_ptr<deluge::project::DocParamSet> domStore_;

	/// External, identity-keyed persistent cable store (the owner's project-tree `cables` vector), or
	/// nullptr when this set resolves its cables inline. Non-owning — the project tree owns it and outlives this
	/// engine collection. See bindDomCablesExternal().
	std::vector<deluge::project::DocCable>* domCablesRef_ = nullptr;

	Destination* destinations[2]{};

	/// @brief Append a cable, growing the storage to fit it.
	///
	/// It joins the unusable end of the list, where the render doesn't reach it, until setupPatching() places it.
	/// @return The new cable, default-initialised; nullptr at kMaxNumPatchCables cables, or if out of memory.
	PatchCable* addCable();

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
	/// @brief The tree entry for the cable (from, dest) in the bound external store, creating one if this is a
	///        runtime-added cable not yet mirrored.
	///
	/// The store is growable; no engine object holds a pointer into it, so reallocation dangles nothing.
	/// @pre domCablesRef_ != nullptr.
	/// @param from     The cable's source.
	/// @param dest     The cable's destination descriptor.
	/// @param polarity The cable's polarity (used when creating the entry).
	/// @return The matching (or newly created) entry.
	deluge::project::DocCable& findOrCreateDomCable(PatchSource from, ParamDescriptor dest, Polarity polarity);
	/// @brief The current amount-nodes for the cable (from, dest) in the bound external store.
	///
	/// Read-only; used to copy live nodes when cloning a bound set.
	/// @param from The cable's source.
	/// @param dest The cable's destination descriptor.
	/// @return The amount-nodes, or nullptr if unbound or not present.
	deluge::project::DocAutoParam* findDomCableAmount(PatchSource from, ParamDescriptor dest) const;

	/// Move the cables into storage for exactly @p count of them, default-initialising any new ones and destroying
	/// any dropped. @return false, with nothing changed, if the storage couldn't be allocated.
	bool resizeCables(uint8_t count);
	void swapCables(int32_t c1, int32_t c2);
	void freeDestinationMemory(bool destructing);
	/// @brief Deep-copy a sentinel-terminated Destination array (terminated by an entry with sources == 0).
	/// @param src The array to copy; may be nullptr.
	/// @return The copy, or nullptr if @p src is nullptr.
	/// @note Throws deluge::exception::BAD_ALLOC on OOM.
	static Destination* cloneDestinations(Destination const* src);
};
