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
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "storage/storage_manager.h"
#include <array>
#include <memory>

namespace deluge::project {
struct DocParamSet;
}

class Sound;
class ParamManagerForTimeline;
class TimelineCounter;
class ModelStackWithParamCollection;

// ParamSet specifies a lot of stuff about how the params will be stored - there's always a fixed number, and they don't
// need other info stored besides their index - like MIDI CC, or patch cable details. This differs from other inheriting
// classes of ParamCollection.

class ParamSet : public ParamCollection {
protected:
	/// Number of parameters in the params array
	int32_t numParams_;

	/// DOM-owned node store this set's AutoParams borrow (Phase C / Model A). nullptr = unbound (params use
	/// their own inline storage, exactly as before). RAII-freed with the set; because the set is heap-allocated
	/// and never relocates — a ParamBackup steal moves only the owning pointer — the store outlives the clip and
	/// follows the params through backup/restore. See docs/superpowers/specs §5.1 (accessor/projection design).
	std::unique_ptr<deluge::project::DocParamSet> domStore_;

	/// M2-E: non-owning pointer to the ACTIVE DOM store this set resolves params against — either `domStore_.get()`
	/// (clip/drum: this set owns it) or an externally-owned store (song-global: `project.global_fx.params.unpatched`,
	/// owned by the DOM project at a stable address). The owner-resolution path (`docForParam`) reads this, so both
	/// binding regimes resolve identically without the AutoParam holding a pointer. nullptr = unbound.
	deluge::project::DocParamSet* domStoreRef_ = nullptr;

public:
	AutoParam* params;
	ParamSet(int32_t newObjectSize, ParamCollectionSummary* summary);
	/// Copy leaves the clone UNBOUND (domStore_ null): a cloned set owns its own nodes until separately bound, so
	/// two sets never alias one DOM store. Out-of-line so DocParamSet can stay incomplete in this header.
	ParamSet(ParamSet const& other);
	~ParamSet() override;

	/// Bind this set's node storage to a DOM-owned DocParamSet (allocated here, RAII-freed with the set): each
	/// AutoParam borrows its nodes from domStore_->params[p].nodes. Idempotent — a no-op if already bound.
	void bindDomStore();
	/// Bind this set's owner-resolution to an EXTERNALLY-owned DOM store (song-global: the store lives in the DOM
	/// project, not here). Sets only the non-owning `domStoreRef_`; `domStore_` stays null so this set never frees
	/// a store it does not own. The caller is responsible for having moved the params' nodes into `ext` already.
	void bindDomStoreExternal(deluge::project::DocParamSet* ext);
	/// The bound DOM store (owned or external), or nullptr if unbound. (Test / inspection accessor.)
	deluge::project::DocParamSet* domStore();

	/// Option I (read-authority): after a DOM-first read populated `domStore()`, project every engine RT cursor
	/// from the store's current_value and set the summary's automated bit exactly as the engine reader
	/// (readLiveParam) would — the nodes live in the store, but playback iterates the summary cache. Requires a
	/// bound store.
	void projectFromStoreAfterRead(ParamCollectionSummary* summary);

	// M2-E: resolve a param's DOM node-store handle from this set (owner-rooted). docForParam takes the array
	// index; docParamForId takes the ModelStack paramId (== index for a ParamSet).
	deluge::project::DocAutoParam* docForParam(int32_t p);
	deluge::project::DocAutoParam* docParamForId(int32_t paramId) override { return docForParam(paramId); }
	[[nodiscard]] bool domStoreIsExternal() const override { return domStore_ == nullptr && domStoreRef_ != nullptr; }

	inline int32_t getValue(int32_t p) { return params[p].getCurrentValue(); }
	int32_t getValueAtPos(int32_t p, uint32_t pos, TimelineCounter* playPositionCounter);
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
	                       bool didPingpong, bool mayInterpolate) final;
	void writeParamAsAttribute(Serializer& writer, char const* name, int32_t p, bool writeAutomation,
	                           bool onlyIfContainsSomething = false, int32_t* valuesForOverride = nullptr);
	void readParam(Deserializer& reader, ParamCollectionSummary* summary, int32_t p, int32_t readAutomationUpToPos);
	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) final;
	void tickTicks(int32_t numTicks, ModelStackWithParamCollection* modelStack) final;
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) final;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) final;
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack) final;
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong) final;
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) final;
	void stripAutomation() override;
	void flattenReverseAutomation(int32_t reverseDirectionWithLength) override;
	void copyOverridingFrom(ParamSet* otherParamSet);
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching) final;
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) final;
	void paramHasAutomationNow(ParamCollectionSummary* summary, int32_t p);
	void paramHasNoAutomationNow(ModelStackWithParamCollection const* modelStack, int32_t p);

	void shiftParamValues(int32_t p, int32_t offset);
	void shiftParamVolumeByDB(int32_t p, float offset);
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength) final;
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	void deleteAutomationForParamBasicForSetup(ModelStackWithParamCollection* modelStack, int32_t p);
	void insertTime(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t lengthToInsert);
	void deleteTime(ModelStackWithParamCollection* modelStack, int32_t startPos, int32_t lengthToDelete);
	void backUpAllAutomatedParamsToAction(Action* action, ModelStackWithParamCollection* modelStack);
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) final;

	// For undoing / redoing
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack) final;

	int32_t getNumParams() { return numParams_; }

	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = true) final;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;

	uint8_t topUintToRepParams;

private:
	void backUpParamToAction(int32_t p, Action* action, ModelStackWithParamCollection* modelStack);
	void checkWhetherParamHasInterpolationNow(ModelStackWithParamCollection const* modelStack, int32_t p);
};

class UnpatchedParamSet final : public ParamSet {
public:
	UnpatchedParamSet(ParamCollectionSummary* summary);
	// Deep copy: copy-constructs the inline params_ array (real AutoParam copies) and re-points params.
	UnpatchedParamSet(UnpatchedParamSet const& other);
	Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
	bool shouldInterpolateWithFloat(ModelStackWithParamId const* modelStack) override;
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override;
	bool doesParamIdAllowAutomation(ModelStackWithParamId const* modelStack) override;
	bool shouldRecordUnautomatedParamChange(ModelStackWithParamId const* modelStack) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	deluge::modulation::params::Kind getParamKind() override { return kind; }

	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;

private:
	std::array<AutoParam, deluge::modulation::params::kMaxNumUnpatchedParams> params_{};
};

class PatchedParamSet final : public ParamSet {
public:
	PatchedParamSet(ParamCollectionSummary* summary);
	PatchedParamSet(PatchedParamSet const& other);
	Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override;
	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::PATCHED; }

private:
	std::array<AutoParam, deluge::modulation::params::kNumParams> params_{};
};

class ExpressionParamSet final : public ParamSet {
public:
	ExpressionParamSet(ParamCollectionSummary* summary, bool forDrum = false);
	ExpressionParamSet(ExpressionParamSet const& other);
	Error clone(ParamCollection*& result, bool copyAutomation, int32_t reverseDirectionWithLength = 0) const override;
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow) override;
	bool mayParamInterpolate(int32_t paramId) override { return false; }
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) override;
	int32_t paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) override;
	bool writeToFile(Serializer& writer, bool mustWriteOpeningTagEndFirst = false);
	void readFromFile(Deserializer& reader, ParamCollectionSummary* summary, int32_t readAutomationUpToPos);
	void moveRegionHorizontally(ModelStackWithParamCollection* modelStack, int32_t pos, int32_t length, int32_t offset,
	                            int32_t lengthBeforeLoop, Action* action);
	void clearValues(ModelStackWithParamCollection const* modelStack);
	void cancelAllOverriding();
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack) override;
	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::EXPRESSION; }

	// bendRanges being stored here in ExpressionParamSet still seems like the best option. I was thinking storing them
	// in the ParamManager would make more sense, except for one thing
	// - persistence when preset/Instrument changes. ExpressionParamSets do this unique thing where they normally aren't
	// "stolen" or "backed up" - unless the last Clip is being deleted, in which case they do move to the
	// backedUpParamManager. This is exactly the persistence we want for bendRanges too.
	uint8_t bendRanges[2]{};

private:
	std::array<AutoParam, kNumExpressionDimensions> params_{};
};
