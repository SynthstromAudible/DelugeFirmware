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
#include "modulation/params/param_collection.h"
#include "definitions.h"
#include "modulation/patch/patch_cable.h"

class Song;
class ModelStackWithParamCollection;
class LearnedMIDI;

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
	~PatchCableSet();

	void setupPatching(ModelStackWithParamCollection const* modelStack);
	bool doesDestinationDescriptorHaveAnyCables(ParamDescriptor destinationParamDescriptor);
	uint8_t getPatchCableIndex(uint8_t from, ParamDescriptor destinationParamDescriptor,
	                           ModelStackWithParamCollection const* modelStack = NULL, bool createIfNotFound = false);
	void deletePatchCable(ModelStackWithParamCollection const* modelStack, uint8_t c);
	bool patchCableIsUsable(uint8_t c, ModelStackWithThreeMainThings const* modelStack);
	int32_t getModifiedPatchCableAmount(int c, int p);
	void removeAllPatchingToParam(ModelStackWithParamCollection* modelStack, uint8_t p);
	bool isSourcePatchedToSomething(int s);
	bool isSourcePatchedToSomethingManuallyCheckCables(int s);
	bool doesParamHaveSomethingPatchedToIt(int p);

	void tickSamples(int numSamples, ModelStackWithParamCollection* modelStack);
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed);
	void playbackHasEnded(ModelStackWithParamCollection* modelStack);
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack);
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally);
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching);
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength);
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int ticksSkipped, bool reversed, bool didPingpong,
	                       bool mayInterpolate);
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength);
	ParamManagerForTimeline* getParamManager();

	void writePatchCablesToFile(bool writeAutomation);
	void readPatchCablesFromFile(int32_t readAutomationUpToPos);
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack);
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack);

	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack);
	AutoParam* getParam(ModelStackWithParamCollection const* modelStack, int s,
	                    ParamDescriptor destinationParamDescriptor, bool allowCreation = false);
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = false);
	static int getParamId(ParamDescriptor destinationParamDescriptor, int s);

	AutoParam* getParam(int paramId);

	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack);

	int paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack);
	int32_t knobPosToParamValue(int knobPos, ModelStackWithAutoParam* modelStack);
	bool isSourcePatchedToDestinationDescriptorVolumeInspecific(int s, ParamDescriptor destinationParamDescriptor);
	bool isAnySourcePatchedToParamVolumeInspecific(ParamDescriptor destinationParamDescriptor);
	void grabVelocityToLevelFromMIDIInput(LearnedMIDI* midiInput);
	void grabVelocityToLevelFromMIDIDeviceDefinitely(MIDIDevice* device);
	PatchCable* getPatchCableFromVelocityToLevel();

	Destination* getDestinationForParam(int p);

	uint32_t sourcesPatchedToAnything[2]; // Only valid after setupPatching()

	PatchCable patchCables[MAX_NUM_PATCH_CABLES]; // TODO: store these in dynamic memory.
	uint8_t numUsablePatchCables;
	uint8_t numPatchCables;

	Destination* destinations[2];

private:
	static void dissectParamId(uint32_t paramId, ParamDescriptor* destinationParamDescriptor, int* s);
	void swapCables(int c1, int c2);
	void freeDestinationMemory(bool destructing);
};
