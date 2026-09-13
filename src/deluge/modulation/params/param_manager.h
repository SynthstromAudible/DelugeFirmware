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
#include "modulation/params/param_collection_summary.h"
#include <cstdint>

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
class PatchCableSet;
class MIDIParamCollection;
class PatchedParamSet;
class UnpatchedParamSet;
class ParamManagerForTimeline;
class ParamCollectionSummary;

#define PARAM_COLLECTIONS_STORAGE_NUM 5

enum class ParamManagerType : uint8_t {
	ANY_MAIN,
	ANY,
	SOUND,
	GLOBAL,
	MIDI,
	CV,
	NONE,
};

class ParamManager {
public:
	ParamManager();
	~ParamManager();

private:
	// Not including MPE params
	// Main param collections start at offset 0, expression can start at 1 or 3 depending on the type (MIDI or Sound
	// respectively)
	inline bool containsAnyMainParamCollections() const {
		return expressionParamSetOffset && summaries[0].paramCollection != nullptr;
	}

	// If there are no main param collections then expression could be stored at offset 0
	inline bool containsAnyParamCollectionsIncludingExpression() const {
		return summaries[0].paramCollection != nullptr;
	}

	inline deluge::modulation::params::Kind param_kind_at_offset(int32_t offset) const {
		return summaries[offset].paramCollection != nullptr ? summaries[offset].paramCollection->getParamKind()
		                                                    : deluge::modulation::params::Kind::NONE;
	}

	// Expression is optional. The following slot must terminate the collection list.
	inline bool has_optional_expression_at(int32_t offset) const {
		if (summaries[offset].paramCollection
		    && param_kind_at_offset(offset) != deluge::modulation::params::Kind::EXPRESSION) {
			return false;
		}
		for (int32_t i = offset + 1; i < PARAM_COLLECTIONS_STORAGE_NUM; ++i) {
			if (summaries[i].paramCollection) {
				return false;
			}
		}
		return true;
	}

	inline bool is_valid_for_sound() const {
		return expressionParamSetOffset == 3
		       && param_kind_at_offset(0) == deluge::modulation::params::Kind::UNPATCHED_SOUND
		       && param_kind_at_offset(1) == deluge::modulation::params::Kind::PATCHED
		       && param_kind_at_offset(2) == deluge::modulation::params::Kind::PATCH_CABLE
		       && has_optional_expression_at(3);
	}

	inline bool is_valid_for_global() const {
		return expressionParamSetOffset == 1
		       && param_kind_at_offset(0) == deluge::modulation::params::Kind::UNPATCHED_GLOBAL
		       && has_optional_expression_at(1);
	}

	inline bool is_valid_for_midi() const {
		return expressionParamSetOffset == 1 && param_kind_at_offset(0) == deluge::modulation::params::Kind::MIDI
		       && has_optional_expression_at(1);
	}

	inline bool has_no_main_collections() const {
		return expressionParamSetOffset == 0 && has_optional_expression_at(0);
	}

public:
	// Presence queries (ANY / ANY_MAIN) are not layout validation. Use this for operations
	// which accept any complete layout, including an empty or expression-only manager.
	inline bool has_valid_layout() const {
		return is_valid_for_sound() || is_valid_for_global() || is_valid_for_midi() || has_no_main_collections();
	}

	inline bool matches_type(ParamManagerType type) const {
		switch (type) {
		case ParamManagerType::ANY_MAIN:
			return containsAnyMainParamCollections();
		case ParamManagerType::ANY:
			return containsAnyParamCollectionsIncludingExpression();
		case ParamManagerType::SOUND: // synth, sound drum
			return is_valid_for_sound();
		case ParamManagerType::GLOBAL: // song, audio clip, kit affect entire
			return is_valid_for_global();
		case ParamManagerType::MIDI: // midi clip
			return is_valid_for_midi();
		case ParamManagerType::CV: // cv clip
			return has_no_main_collections();
		case ParamManagerType::NONE: // MIDI / GATE drums may retain expression, but have no main collections.
			return has_no_main_collections();
		default:
			break;
		}
		return false;
	}

	Error setupWithPatching();
	Error setupUnpatched();
	Error setupMIDI();

	void stealParamCollectionsFrom(ParamManager* other, bool stealExpressionParams = false);
	Error cloneParamCollectionsFrom(ParamManager const* other, bool copyAutomation, bool cloneExpressionParams = false,
	                                int32_t reverseDirectionWithLength = 0);
	Error beenCloned(int32_t reverseDirectionWithLength = 0); // Will clone Collections
	void forgetParamCollections();
	void destructAndForgetParamCollections();
	void destructMainParamCollections(); // Preserve expression values, automation and bend ranges.
	bool ensureExpressionParamSetExists(bool forDrum = false);

	inline int32_t getExpressionParamSetOffset() { return expressionParamSetOffset; }

	ExpressionParamSet* getOrCreateExpressionParamSet(bool forDrum = false); // Will return NULL if can't create

	inline ParamCollectionSummary* getExpressionParamSetSummary() { // Will return one containing NULL if didn't exist
#if ALPHA_OR_BETA_VERSION
		if ((expressionParamSetOffset != 0 && expressionParamSetOffset != 1 && expressionParamSetOffset != 3)
		    || (summaries[expressionParamSetOffset].paramCollection
		        && param_kind_at_offset(expressionParamSetOffset) != deluge::modulation::params::Kind::EXPRESSION)) {
			FREEZE_WITH_ERROR("PM0D");
		}
#endif
		return &summaries[getExpressionParamSetOffset()];
	}

	inline ExpressionParamSet* getExpressionParamSet() { // Will return NULL if didn't exist
		return (ExpressionParamSet*)getExpressionParamSetSummary()->paramCollection;
	}

	inline MIDIParamCollection* getMIDIParamCollection() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(0) != deluge::modulation::params::Kind::MIDI) {
			FREEZE_WITH_ERROR("PM00"); // was E409
		}
#endif
		return (MIDIParamCollection*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getMIDIParamCollectionSummary() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(0) != deluge::modulation::params::Kind::MIDI) {
			FREEZE_WITH_ERROR("PM01"); // was E409
		}
#endif
		return &summaries[0];
	}

	inline UnpatchedParamSet* getUnpatchedParamSet() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(0) != deluge::modulation::params::Kind::UNPATCHED_SOUND
		    && param_kind_at_offset(0) != deluge::modulation::params::Kind::UNPATCHED_GLOBAL) {
			FREEZE_WITH_ERROR("PM02"); // was E410
		}
#endif
		return (UnpatchedParamSet*)summaries[0].paramCollection;
	}

	inline ParamCollectionSummary* getUnpatchedParamSetSummary() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(0) != deluge::modulation::params::Kind::UNPATCHED_SOUND
		    && param_kind_at_offset(0) != deluge::modulation::params::Kind::UNPATCHED_GLOBAL) {
			FREEZE_WITH_ERROR("PM03"); // was E410
		}
#endif
		return &summaries[0];
	}

	inline PatchedParamSet* getPatchedParamSet() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(1) != deluge::modulation::params::Kind::PATCHED) {
			FREEZE_WITH_ERROR("PM04"); // was E411
		}
#endif
		return (PatchedParamSet*)summaries[1].paramCollection;
	}

	inline ParamCollectionSummary* getPatchedParamSetSummary() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(1) != deluge::modulation::params::Kind::PATCHED) {
			FREEZE_WITH_ERROR("PM05"); // was E411
		}
#endif
		return &summaries[1];
	}

	inline ParamCollectionSummary* getPatchCableSetSummary() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(2) != deluge::modulation::params::Kind::PATCH_CABLE) {
			FREEZE_WITH_ERROR("PM06"); // was E412
		}
#endif
		return &summaries[2];
	}

	inline PatchCableSet* getPatchCableSet() {
#if ALPHA_OR_BETA_VERSION
		if (param_kind_at_offset(2) != deluge::modulation::params::Kind::PATCH_CABLE) {
			FREEZE_WITH_ERROR("PM07"); // was E412
		}
#endif
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	ModelStackWithParamCollection* getPatchCableSet(ModelStackWithThreeMainThings const* modelStack);

	inline PatchCableSet* getPatchCableSetAllowJibberish() { // Don't ask.
		return (PatchCableSet*)summaries[2].paramCollection;
	}

	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t currentValueChanged,
	                                  bool automationChanged, bool paramAutomatedNow);

#if ALPHA_OR_BETA_VERSION
	virtual ParamManagerForTimeline* toForTimeline();
#else
	inline ParamManagerForTimeline* toForTimeline() { return (ParamManagerForTimeline*)this; }
#endif

	bool resonanceBackwardsCompatibilityProcessed;
	uint8_t expressionParamSetOffset;

	// This list should be terminated by an object whose values are all zero. Yes, all of them must be zero, because if
	// we know this, we can check for stuff faster.
	ParamCollectionSummary summaries[PARAM_COLLECTIONS_STORAGE_NUM];
};

class ParamManagerForTimeline final : public ParamManager { // I want to rename this one to "with automation"
public:
	ParamManagerForTimeline();

	void tickSamples(int32_t numSamples, ModelStackWithThreeMainThings* modelStack);
	void setPlayPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack, bool reversed);
	void expectNoFurtherTicks(ModelStackWithThreeMainThings* modelStack);
	void grabValuesFromPos(uint32_t pos, ModelStackWithThreeMainThings* modelStack);
	void generateRepeats(ModelStackWithThreeMainThings* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	void appendParamManager(ModelStackWithThreeMainThings* modelStack, ModelStackWithThreeMainThings* otherModelStack,
	                        int32_t oldLength, int32_t reverseThisRepeatWithLength, bool pingpongingGenerally);
	void trimToLength(uint32_t newLength, ModelStackWithThreeMainThings* modelStack, Action* action,
	                  bool maySetupPatching = true);

	void processCurrentPos(ModelStackWithThreeMainThings* modelStack, int32_t ticksSinceLast, bool reversed,
	                       bool didPingpong = false, bool mayInterpolate = true);
	void expectEvent(ModelStackWithThreeMainThings const* modelStack);

	void shiftHorizontally(ModelStackWithThreeMainThings* modelStack, int32_t amount, int32_t effectiveLength);
	void nudgeAutomationHorizontallyAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                      ModelStackWithThreeMainThings* modelStack, bool nudgeAutomation,
	                                      bool nudgeMPE, int32_t moveMPEDataWithinRegionLength = 0);
	void deleteAllAutomation(Action* action, ModelStackWithThreeMainThings* modelStack);
	void notifyPingpongOccurred(ModelStackWithThreeMainThings* modelStack);
	void ensureSomeParamCollections(); // For debugging only
	bool mightContainAutomation();

#if ALPHA_OR_BETA_VERSION
	ParamManagerForTimeline* toForTimeline() override;
#endif

	int32_t ticksTilNextEvent;
	int32_t ticksSkipped;
};
