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

class Clip;
class ModelStackWithParamCollection;
class Serializer;
class Deserializer;

class MIDIParamCollection final : public ParamCollection {
public:
	MIDIParamCollection(ParamCollectionSummary* summary);
	~MIDIParamCollection() override;
	/// to avoid spamming midi we interpolate in ticks instead of in samples like internal synths. This is mostly
	/// unnoticeable but limits the amount of data sent
	void tickTicks(int32_t numSamples, ModelStackWithParamCollection* modelStack) override;

	void tickSamples(int32_t numSamples, ModelStackWithParamCollection* modelStack) override {};
	void setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) override;
	void playbackHasEnded(ModelStackWithParamCollection* modelStack) override {}
	void generateRepeats(ModelStackWithParamCollection* modelStack, uint32_t oldLength, uint32_t newLength,
	                     bool shouldPingpong);
	void appendParamCollection(ModelStackWithParamCollection* modelStack,
	                           ModelStackWithParamCollection* otherModelStack, int32_t oldLength,
	                           int32_t reverseThisRepeatWithLength, bool pingpongingGenerally);
	void trimToLength(uint32_t newLength, ModelStackWithParamCollection* modelStack, Action* action,
	                  bool maySetupPatching);
	void shiftHorizontally(ModelStackWithParamCollection* modelStack, int32_t amount, int32_t effectiveLength);
	void processCurrentPos(ModelStackWithParamCollection* modelStack, int32_t ticksSkipped, bool reversed,
	                       bool didPingpong, bool mayInterpolate);
	void remotelySwapParamState(AutoParamState* state, ModelStackWithParamId* modelStack);
	void deleteAllAutomation(Action* action, ModelStackWithParamCollection* modelStack);
	Error makeInterpolatedCCsGoodAgain(int32_t clipLength);
	void grabValuesFromPos(uint32_t pos, ModelStackWithParamCollection* modelStack);
	void nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
	                                     ModelStackWithParamCollection* modelStack) override;
	ModelStackWithAutoParam* getAutoParamFromId(ModelStackWithParamId* modelStack, bool allowCreation = true) override;
	static int32_t autoparamValueToCC(int32_t newValue);

	void cloneFrom(ParamCollection* otherParamSet, bool copyAutomation);
	void beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength);
	void sendMIDI(MIDISource source, int32_t channel, int32_t cc, int32_t newValue, int32_t midiOutputFilter);
	void notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
	                                  bool automationChanged, bool automatedBefore, bool automatedNow);
	bool mayParamInterpolate(int32_t paramId);
	int32_t knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack);
	void notifyPingpongOccurred(ModelStackWithParamCollection* modelStack);

	void writeToFile(Serializer& writer);
	int32_t moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithParamCollection* modelStack);

	deluge::modulation::params::Kind getParamKind() { return deluge::modulation::params::Kind::MIDI; }

	MIDIParamVector params;

private:
	void deleteAllParams(Action* action = NULL, bool deleteStorageToo = true);
};
