/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include "model/drum/non_audio_drum.h"
#include "util/containers.h"
#include <array>
#include <string_view>

class MIDIDrum final : public NonAudioDrum {
public:
	MIDIDrum();

	void noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, int16_t const* mpeValues,
	            int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0) override;
	void noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity = kDefaultLiftValue) override;
	void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) override;
	void noteOffPostArp(int32_t noteCodePostArp) override;
	void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager) override;
	Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) override;
	void getName(char* buffer) override;
	int32_t getNumChannels() override { return 16; }
	void killAllVoices() override;

	int8_t modEncoderAction(ModelStackWithThreeMainThings* modelStack, int8_t offset, uint8_t whichModEncoder) override;

	void expressionEvent(int32_t newValue, int32_t expressionDimension) override;

	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                              int32_t channelOrNoteNumber,
	                                              MIDICharacteristic whichCharacteristic) override;

	// MIDI CC automation support (similar to MIDIInstrument)
	void ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack);
	void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue, ModelStackWithTimelineCounter* modelStack);
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t cc,
	                                                               ModelStackWithThreeMainThings* modelStack);
	int32_t changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode);
	int32_t getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
	                         int32_t stopAt);
	Error moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack);
	int32_t moveAutomationToDifferentCC(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode,
	                                    ModelStackWithThreeMainThings* modelStack);
	bool doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int32_t cc);
	Error readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos,
	                                     ParamManagerForTimeline* paramManager = nullptr);

	// ModControllable implementation
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) override;
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) override;
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) override;
	uint8_t* getModKnobMode() override { return &modKnobMode; }
	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
	bool valueChangedEnoughToMatter(int32_t old_value, int32_t new_value, deluge::modulation::params::Kind kind,
	                                uint32_t paramID) override;

	// Device definition support (similar to MIDIInstrument)
	std::string_view getNameFromCC(int32_t cc);
	void setNameForCC(int32_t cc, std::string_view name);
	String deviceDefinitionFileName;
	bool loadDeviceDefinitionFile = false;
	deluge::fast_map<uint8_t, std::string> labels;

	// CC sending method
	void sendCC(int32_t cc, int32_t value);

	uint8_t note;
	int8_t noteEncoderCurrentOffset;

	// MIDI output device selection - 0 means send to all devices (current behavior)
	// 1 = DIN, 2+ = USB devices (device index + 1)
	uint8_t outputDevice{0};

	// Mod knob CC assignments (same as MIDI instruments)
	std::array<int8_t, kNumModButtons * kNumPhysicalModKnobs> modKnobCCAssignments;
	uint8_t modKnobMode = 0;
};
