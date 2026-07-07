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
struct DocPatchCable;
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
	// Deep copy: real-copies each usable cable's automation and the destination arrays (see clone()).
	PatchCableSet(PatchCableSet const& other);
	~PatchCableSet() override;

	/// Phase C (DOM single-store, Model A): borrow-point each present patch cable's amount-automation nodes at a
	/// DOM store anchored on this (heap-stable) collection. Sized once to numPatchCables; cables added LATER stay
	/// unbound (self-owned). Idempotent. Like MIDI-CC, this is a non-ParamSet owner with its own store.
	void bindDomStore();
	/// The bound DOM store, or nullptr if unbound. (Test / inspection accessor.)
	deluge::project::DocParamSet* domStore();

	/// S4b: bind this set's cable amount-automation to an external, identity-keyed persistent store (the owner's
	/// project-tree `DocParamManager::patch_cables`). Once bound, docForParam resolves a cable's nodes from the tree
	/// entry matching its (from, destination) — save + runtime edits share it — instead of the cable's inline
	/// `amount_`. Cables are matched by identity, not index, so runtime reorder (setupPatching) is transparent.
	void bindDomCablesExternal(std::vector<deluge::project::DocPatchCable>* ext) { domCablesRef_ = ext; }
	/// The bound external cable store, or nullptr if unbound. (Test / inspection accessor.)
	std::vector<deluge::project::DocPatchCable>* domCables() { return domCablesRef_; }

	deluge::project::DocAutoParam* docForParam(int32_t i);
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
	void readPatchCablesFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
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

	PatchCable patchCables[kMaxNumPatchCables]; // TODO: store these in dynamic memory.
	uint8_t numUsablePatchCables;
	uint8_t numPatchCables;

	/// DOM-owned node store the cable amount-AutoParams borrow (nullptr = unbound). RAII-freed with this
	/// (heap-allocated, stable-address) collection. See bindDomStore().
	std::unique_ptr<deluge::project::DocParamSet> domStore_;

	/// S4b: external, identity-keyed persistent cable store (the owner's project-tree `patch_cables` vector), or
	/// nullptr when this set resolves its cables inline. Non-owning — the project tree owns it and outlives this
	/// engine collection. See bindDomCablesExternal().
	std::vector<deluge::project::DocPatchCable>* domCablesRef_ = nullptr;

	Destination* destinations[2]{};

	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override { return true; };

	static void dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, PatchSource* s);

private:
	/// S4b: the tree entry for the cable (from, dest) in the bound external store, creating one (growable — no
	/// engine object holds a pointer into the store, so reallocation dangles nothing) if this is a runtime-added
	/// cable not yet mirrored. PRECONDITION: domCablesRef_ != nullptr.
	deluge::project::DocPatchCable& findOrCreateDomCable(PatchSource from, ParamDescriptor dest, Polarity polarity);
	/// S4b: the current amount-nodes for the cable (from, dest) in the bound external store, or nullptr if unbound
	/// or not present. Read-only (used to copy live nodes when cloning a bound set).
	deluge::project::DocAutoParam* findDomCableAmount(PatchSource from, ParamDescriptor dest) const;

	void swapCables(int32_t c1, int32_t c2);
	void freeDestinationMemory(bool destructing);
	/// Deep-copy a sentinel-terminated Destination array (terminated by an entry with sources == 0). Returns
	/// nullptr if src is nullptr. Throws deluge::exception::BAD_ALLOC on OOM.
	static Destination* cloneDestinations(Destination const* src);
};
