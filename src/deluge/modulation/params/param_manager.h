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

#include "arp/params.h"
#include "definitions_cxx.hpp"
#include "modulation/params/param_collection_summary.h"
#include <cstdint>
#include <optional>

class Song;
class Sound;
class ParamSet;
class InstrumentClip;
class Action;
class ParamCollection;
class TimelineCounter;
class Output;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class ModelStackWithParamCollection;
class ExpressionParamSet;
class ArpParamSet;
class PatchCableSet;
class MIDIParamCollection;
class PatchedParamSet;
class UnpatchedParamSet;
/// The arguments of one repeat's append to a take's param manager (deluge::modulation::catchUpArrangementTakes()).
struct RepeatArgs {
	int32_t offset;                      ///< where the repeat goes: the length already there
	int32_t reverseThisRepeatWithLength; ///< 0 to copy the repeat straight, else the length to mirror it within
	bool pingpongingGenerally;           ///< whether a pingpong direction flip applies
};

class ParamManagerForTimeline;
namespace deluge::bridge {
struct LaneTarget;
} // namespace deluge::bridge
class ParamCollectionSummary;

#define PARAM_COLLECTIONS_STORAGE_NUM 6

class ParamManager {
public:
	ParamManager();
	~ParamManager();

	// Moves transfer ownership of the ParamCollections, emptying the source. Copying must stay explicit
	// (cloneParamCollectionsFrom / beenCloned), as it can fail.
	ParamManager(ParamManager&& other) noexcept { moveFrom(other); }
	ParamManager& operator=(ParamManager&& other) noexcept {
		if (this != &other) {
			destructAndForgetParamCollections();
			moveFrom(other);
		}
		return *this;
	}
	ParamManager(ParamManager const&) = delete;
	ParamManager& operator=(ParamManager const&) = delete;

	/// @brief Whether the manager holds an unpatched, patched or MIDI collection: the arp's set and MPE params alone do
	///        not count (a MIDI or gate kit row's manager holds only those).
	inline bool containsAnyMainParamCollections() { return expressionParamSetOffset != 0 && arpParamSetOffset != 0; }

	/// @brief Borrow-point this manager's ParamSets at DOM-owned DocParamSets anchored on the sets.
	///
	/// Binds unpatched always, patched if present, and expression (MPE) if present. Idempotent. Audio/kit
	/// clip-level managers bind their unpatched set only; synth/drum managers add patched (+ expression).
	///
	/// @pre Only call on a Sound or host chain manager (summaries[0] is an UnpatchedParamSet).
	/// @warning A MIDI manager also reports containsAnyMainParamCollections()==true but holds a
	///          MIDIParamCollection at summaries[0] (not a ParamSet) -- binding it would corrupt it. Callers gate
	///          on SYNTH/KIT clips + SoundDrum.
	void bindMainParamSetDomStores();

	inline bool containsAnyParamCollectionsIncludingExpression() { return summaries[0].paramCollection; }

	/// @brief A sound's collections: unpatched, patched, cables, then the sound's arp.
	Error setupWithPatching();
	/// @brief An unpatched set, then the arp's set of @p arp where the owner holds an arp (a kit's own arp).
	Error setupUnpatched(std::optional<deluge::arp::Owner> arp = std::nullopt);
	/// @brief A MIDI clip's collections: its CC collection, then its arp's set.
	Error setupMIDI();
	/// @brief The arp's set alone (a MIDI or gate kit row's): it does not count as a main collection.
	Error setupArpOnly(deluge::arp::Owner arp);

	void stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams = false);
	Error cloneParamCollectionsFrom(ParamManager const* other, bool copyAutomation, bool cloneExpressionParams = false,
	                                int32_t reverseDirectionWithLength = 0);
	/// @brief Reverse all collections' automation in place.
	///
	/// Used when a cloned NoteRow must flatten a reversed sequence; the collections were copied
	/// reverse-agnostically.
	/// @param reverseDirectionWithLength The loop length.
	void flattenReverseAutomation(int32_t reverseDirectionWithLength);
	void forgetParamCollections();
	void destructAndForgetParamCollections();
	void destructAndForgetMainParamCollections(); // Retains expression params, like forgetParamCollections()
	bool ensureExpressionParamSetExists(bool forDrum = false);

	inline int32_t getExpressionParamSetOffset() { return expressionParamSetOffset; }

	ExpressionParamSet* getOrCreateExpressionParamSet(bool forDrum = false); // Will return NULL if can't create

	inline ParamCollectionSummary* getExpressionParamSetSummary() { // Will return one containing NULL if didn't exist
		return &summaries[getExpressionParamSetOffset()];
	}

	inline ExpressionParamSet* getExpressionParamSet() { // Will return NULL if didn't exist
		return (ExpressionParamSet*)getExpressionParamSetSummary()->paramCollection;
	}

	/// @brief The arp's set, or nullptr for an owner that holds no arp (the song, an audio clip, a host chain).
	inline ArpParamSet* getArpParamSet() {
		ParamCollectionSummary* summary = getArpParamSetSummary();
		return summary != nullptr ? reinterpret_cast<ArpParamSet*>(summary->paramCollection) : nullptr;
	}

	/// @brief The arp's set's summary, or nullptr for an owner that holds no arp.
	inline ParamCollectionSummary* getArpParamSetSummary() {
		return arpParamSetOffset != kNoArpSlot ? &summaries[arpParamSetOffset] : nullptr;
	}

	inline MIDIParamCollection* getMIDIParamCollection() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E409");
		}
		return (MIDIParamCollection*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getMIDIParamCollectionSummary() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E409");
		}
		return &summaries[0];
	}

	inline UnpatchedParamSet* getUnpatchedParamSet() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E410");
		}
		return (UnpatchedParamSet*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getUnpatchedParamSetSummary() {
		if (!summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E410");
		}
		return &summaries[0];
	}

	inline PatchedParamSet* getPatchedParamSet() {
		if (!summaries[1].paramCollection) {
			FREEZE_WITH_ERROR("E411");
		}
		return (PatchedParamSet*)summaries[1].paramCollection;
	}

	inline ParamCollectionSummary* getPatchedParamSetSummary() {
		if (!summaries[1].paramCollection) {
			FREEZE_WITH_ERROR("E411");
		}
		return &summaries[1];
	}

	inline ParamCollectionSummary* getPatchCableSetSummary() {
		if (!summaries[2].paramCollection) {
			FREEZE_WITH_ERROR("E412");
		}
		return &summaries[2];
	}

	inline PatchCableSet* getPatchCableSet() {
		if (!summaries[2].paramCollection) {
			FREEZE_WITH_ERROR("E412");
		}
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	ModelStackWithParamCollection* getPatchCableSet(ModelStackWithThreeMainThings const* modelStack);

	inline PatchCableSet* getPatchCableSetAllowJibberish() { // Don't ask.
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	void notifyParamModifiedInSomeWay(const deluge::bridge::LaneTarget& lane, int32_t currentValueChanged,
	                                  bool automationChanged, bool paramAutomatedNow);

	virtual ParamManagerForTimeline* toForTimeline();

	uint8_t expressionParamSetOffset{};

	/// The arp set's index, the last main slot (or 0 for an arp-only manager); kNoArpSlot where there is none.
	static constexpr uint8_t kNoArpSlot = 0xFF;
	uint8_t arpParamSetOffset{kNoArpSlot};

	// This list should be terminated by an object whose values are all zero. Yes, all of them must be zero, because if
	// we know this, we can check for stuff faster.
	ParamCollectionSummary summaries[PARAM_COLLECTIONS_STORAGE_NUM]{};

private:
	void moveFrom(ParamManager& other) noexcept {
		expressionParamSetOffset = other.expressionParamSetOffset;
		arpParamSetOffset = other.arpParamSetOffset;
		for (auto i = 0; i < PARAM_COLLECTIONS_STORAGE_NUM; i++) {
			summaries[i] = other.summaries[i];
			other.summaries[i] = {nullptr};
		}
		other.expressionParamSetOffset = 0;
		other.arpParamSetOffset = kNoArpSlot;
	}
};

class ParamManagerForTimeline final : public ParamManager { // I want to rename this one to "with automation"
public:
	ParamManagerForTimeline();
	ParamManagerForTimeline(ParamManagerForTimeline&&) noexcept = default;
	ParamManagerForTimeline& operator=(ParamManagerForTimeline&&) noexcept = default;

	void tickSamples(int32_t numSamples, ModelStackWithThreeMainThings* modelStack);
	void setPlayPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack, bool reversed);
	void expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack);
	void grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack);
	void generateRepeats(ModelStackWithThreeMainThings* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	void trimToLength(uint32_t newLength, ModelStackWithThreeMainThings* modelStack, Action* action,
	                  bool maySetupPatching = true);

	void processCurrentPos(ModelStackWithThreeMainThings* modelStack, int32_t ticksSinceLast, bool reversed,
	                       bool didPingpong = false, bool mayInterpolate = true);
	void expectEvent(TimelineCounter* timeline);

	void shiftHorizontally(ModelStackWithThreeMainThings* modelStack, int32_t amount, int32_t effectiveLength);
	void nudgeAutomationHorizontallyAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                      ModelStackWithThreeMainThings* modelStack, bool nudgeAutomation,
	                                      bool nudgeMPE, int32_t moveMPEDataWithinRegionLength = 0);
	void deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack);
	void notifyPingpongOccurred(ModelStackWithThreeMainThings* modelStack);
	void ensureSomeParamCollections(); // Freezes if empty - the DEFINITELY_SOME loops require at least one
	bool mightContainAutomation();

	ParamManagerForTimeline* toForTimeline() override;

	int32_t ticksTilNextEvent;
	int32_t ticksSkipped;
};
