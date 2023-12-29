/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "model/global_effectable/global_effectable.h"

class InstrumentClip;
class Clip;
class Kit;
class Drum;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;
enum class MIDIMatchType;

class MidiFollow final : public GlobalEffectable {
public:
	MidiFollow();
	void readDefaultsFromFile();

	//midi follow context
	Clip* getClipForMidiFollow(bool useActiveClip = false);
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
	                                                ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                Clip* clip, int32_t xDisplay, int32_t yDisplay, int32_t ccNumber,
	                                                bool displayError = true);
	void noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note, int32_t velocity,
	                         bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack);
	void midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
	                    bool isMPE, ModelStack* modelStack);
	void pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru,
	                       ModelStack* modelStack);
	void aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                        bool* doingMidiThru, ModelStack* modelStack);

	//midi CC mappings
	int32_t getCCFromParam(Param::Kind paramKind, int32_t paramID);

	int32_t paramToCC[kDisplayWidth][kDisplayHeight];
	int32_t previousKnobPos[kMaxMIDIValue + 1];
	uint32_t timeLastCCSent[kMaxMIDIValue + 1];
	uint32_t timeAutomationFeedbackLastSent;

private:
	//initialize
	void init();
	void initMapping(int32_t mapping[kDisplayWidth][kDisplayHeight]);

	//midi follow
	Clip* clipForLastNoteReceived[kMaxMIDIValue + 1];
	void offerReceivedNoteToKit(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
	                            int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                            bool* doingMidiThru, Clip* clip);
	void offerReceivedNoteToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                          MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note,
	                                          int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru,
	                                          Clip* clip);
	void offerReceivedCCToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                          uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru, Clip* clip);
	void offerReceivedCCToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                        MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                        bool* doingMidiThru, Clip* clip);
	void offerReceivedPitchBendToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                 bool* doingMidiThru, Clip* clip);
	void offerReceivedPitchBendToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                               MIDIDevice* fromDevice, uint8_t channel, uint8_t data1,
	                                               uint8_t data2, bool* doingMidiThru, Clip* clip);
	void offerReceivedAftertouchToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                  MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                                  bool* doingMidiThru, Clip* clip);
	void offerReceivedAftertouchToMelodicInstrument(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                MIDIDevice* fromDevice, int32_t channel, int32_t value,
	                                                int32_t noteCode, bool* doingMidiThru, Clip* clip);

	MIDIMatchType checkMidiFollowMatch(MIDIDevice* fromDevice, uint8_t channel, MIDIFollowChannelType type);
	Drum* getDrumFromNoteCode(Kit* kit, int32_t noteCode);

	//saving
	void writeDefaultsToFile();
	void writeDefaultMappingsToFile();

	//loading
	bool successfullyReadDefaultsFromFile;
	void readDefaultMappingsFromFile();
};

extern MidiFollow midiFollow;
