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
#include <algorithm>
#include <array>

class Song;
struct PatchCablePresetValues;
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
	~PatchCableSet() override;

	ClipParamValue* getClipValueStorage(int32_t paramId) override;
	using ParamCollection::getValueForControl;
	int32_t getValueForControl(int32_t paramId, ModelStackWithThreeMainThings const* modelStack) override;
	void bindPresetValues(const PatchCablePresetValues* values);
	void detachPresetSlot(const int32_t* slot);
	void setupPatching(ModelStackWithParamCollection const* modelStack);
	bool doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor);
	uint8_t getPatchCableIndex(PatchSource from, ParamDescriptor destinationParamDescriptor,
	                           ModelStackWithParamCollection const* modelStack = nullptr,
	                           bool createIfNotFound = false);
	void deletePatchCable(ModelStackWithParamCollection const* modelStack, uint8_t c);
	bool patchCableIsUsable(uint8_t c, ModelStackWithThreeMainThings const* modelStack);
	int32_t getModifiedPatchCableAmount(int32_t c, int32_t p);
	// The strength rendering uses: one load while no value has changed outside playback.
	[[nodiscard]] int32_t getStrengthForRender(int32_t c) const {
		if (resolvedVersion_ != parameter_value_epoch) [[unlikely]] {
			refreshResolved();
		}
#ifdef PARAMETER_RESOLVED_VERIFY
		parameterVerifyReads = parameterVerifyReads + 1;
		if (std::clamp(resolvedStrengths_[c], -PatchCable::kMaxStrength, PatchCable::kMaxStrength)
		    != patchCables[c].getValue()) {
			parameterVerifyMismatches = parameterVerifyMismatches + 1;
			parameterVerifyLastMismatch = 0xC0000000U | static_cast<uint32_t>(c);
		}
#endif
		return std::clamp(resolvedStrengths_[c], -PatchCable::kMaxStrength, PatchCable::kMaxStrength);
	}
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
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) override;
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

	void notifyParamValueModified(ModelStackWithAutoParam const* modelStack, int32_t oldValue, int32_t newValue,
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

	uint32_t sourcesPatchedToAnything[2]; // Only valid after setupPatching()

	PatchCable patchCables[kMaxNumPatchCables]; // TODO: store these in dynamic memory.
	uint8_t numUsablePatchCables;
	uint8_t numPatchCables;

	Destination* destinations[2];

	bool shouldParamIndicateMiddleValue(ModelStackWithParamId const* modelStack) override { return true; };

	static void dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, PatchSource* s);

private:
	[[gnu::noinline]] void refreshResolved() const;
	// Unclamped cable strengths, refreshed whenever the value version moves; playback
	// loops keep the cables they advance up to date.
	mutable std::array<int32_t, kMaxNumPatchCables> resolvedStrengths_{};
	mutable uint32_t resolvedVersion_ = 0;
	uint32_t tickedBaselineEpoch_ = 0;
	const PatchCablePresetValues* presetValues_ = nullptr;
	void swapCables(int32_t c1, int32_t c2);
	void storeHeldRamps();
	void freeDestinationMemory(bool destructing);
};
