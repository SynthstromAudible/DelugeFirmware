/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include "io/midi/midi_engine.h"
#include "modulation/midi/midi_param_vector.h"
#include "modulation/params/param_collection.h"
#include <memory>
#include <vector>

class Clip;
class ModelStackWithParamCollection;
class Serializer;
class Deserializer;

namespace deluge::project {
struct DocParamSet;
struct DocMidiParam;
struct DocAutoParam;
} // namespace deluge::project

class MIDIParamCollection final : public ParamCollection {
public:
	MIDIParamCollection(ParamCollectionSummary* summary);
	/// @brief Copy constructor (used by clone()).
	///
	/// Deep-copies `params` (each MIDIParam with its inline nodes_, the tree's live nodes when the source is bound) and
	/// leaves the clone unbound (domStore_ null): a cloned collection owns its nodes until separately bound.
	///
	/// @param other The collection to copy.
	MIDIParamCollection(MIDIParamCollection const& other);
	~MIDIParamCollection() override;
	/// to avoid spamming midi we interpolate in ticks instead of in samples like internal synths. This is mostly
	/// unnoticeable but limits the amount of data sent
	void tickTicks(int32_t numSamples, ModelStackWithParamCollection* modelStack) override;

	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) override {};
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) override;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) override {}
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
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) override;
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	Error makeInterpolatedCCsGoodAgain(int32_t clipLength);
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) override;
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) override;
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = true) override;
	static int32_t autoparamValueToCC(int32_t newValue);

	/// @copydoc ParamCollection::clone
	Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
	void stripAutomation() override;
	void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
	void sendMIDI(MIDISource source, int32_t channel, int32_t cc, int32_t newValue, int32_t midiOutputFilter);
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	bool mayParamInterpolate(int32_t paramId) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) override;

	void writeToFile(Serializer& writer);
	int32_t moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithParamCollection* modelStack);

	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::MIDI; }

	MIDIParamVector params;

	/// @brief No-op: each CC's nodes live inline in its MIDIParam (see MIDIParam::nodes_), because the
	///        MIDIParamVector reorders by CC and a positional shared store could not stay in sync.
	// TODO: bindDomStore() does nothing and domStore() always returns nullptr; delete both and their callers.
	void bindDomStore();
	/// @brief The bound DOM store (test / inspection accessor).
	/// @return Always nullptr: this collection is never bound to a shared positional store.
	deluge::project::DocParamSet* domStore();

	/// @brief Bind this collection's CC automation to an external, identity-keyed (by cc) persistent store (the
	///        clip's project-tree `DocParamManager::midi_params`).
	///
	/// Once bound, docForParam resolves a CC's nodes from the tree entry matching its cc (save and runtime edits
	/// share it) instead of the CC's inline `nodes_`. Matched by identity, not index, so a newly-learned CC's
	/// reorder of the MIDIParamVector is transparent.
	///
	/// @param ext The project-tree CC store; non-owning, must outlive this collection.
	void bindDomMidiExternal(std::vector<deluge::project::DocMidiParam>* ext) { domMidiRef_ = ext; }
	/// @brief The bound external CC store (test / inspection accessor).
	/// @return The store, or nullptr if unbound.
	std::vector<deluge::project::DocMidiParam>* domMidi() { return domMidiRef_; }
	[[nodiscard]] bool domStoreIsExternal() const override { return domMidiRef_ != nullptr; }

	/// @brief Resolve the node store of the CC at vector index @p i.
	/// @param i Index into `params`.
	/// @return The bound tree entry when bound to an external store, else the CC's inline `nodes_`.
	deluge::project::DocAutoParam* docForParam(int32_t i);
	/// @copydoc ParamCollection::docParamForId
	/// @note The paramId is the CC number.
	deluge::project::DocAutoParam* docParamForId(int32_t paramId) override;

private:
	/// DOM-owned node store (nullptr = unbound). RAII-freed with the collection.
	std::unique_ptr<deluge::project::DocParamSet> domStore_;

	/// External, identity-keyed (by cc) persistent CC store (the clip's project-tree `midi_params` vector), or
	/// nullptr when this collection resolves its CCs inline. Non-owning — the project tree owns it and outlives
	/// this engine collection. See bindDomMidiExternal().
	std::vector<deluge::project::DocMidiParam>* domMidiRef_ = nullptr;

	/// @brief The tree entry for @p cc in the bound external store, created (growable; the AutoParam is a pure
	///        cursor holding no pointer into the store) if this is a runtime-learned CC not yet mirrored.
	/// @pre domMidiRef_ != nullptr.
	/// @param cc The CC number.
	/// @return The entry.
	deluge::project::DocMidiParam& findOrCreateDomMidi(uint8_t cc);
	/// @brief The current nodes for @p cc in the bound external store; read-only use (copying live nodes when
	///        cloning a bound collection).
	/// @param cc The CC number.
	/// @return The nodes, or nullptr if unbound or absent.
	deluge::project::DocAutoParam* findDomMidiNodes(uint8_t cc) const;

	void deleteAllParams(Action* action = NULL, bool deleteStorageToo = true);
};
