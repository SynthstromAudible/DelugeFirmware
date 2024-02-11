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
#include "modulation/params/param.h"

class AudioClip;
class InstrumentClip;
class Clip;
class Kit;
class Drum;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;
enum class MIDIMatchType;

Clip* getSelectedClip(bool useActiveClip = false);

class MidiFollow final {
public:
	MidiFollow();
	void readDefaultsFromFile();

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
	                                                ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                Clip* clip, int32_t xDisplay, int32_t yDisplay, int32_t ccNumber,
	                                                bool displayError = true);
	void noteMessageReceived(MIDIDevice* fromDevice, bool on, int32_t channel, int32_t note, int32_t velocity,
	                         bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack);
	void sendNoteToClip(MIDIDevice* fromDevice, Clip* clip, MIDIMatchType match, bool on, int32_t channel, int32_t note,
	                    int32_t velocity, bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack);
	void midiCCReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
	                    ModelStack* modelStack);
	void pitchBendReceived(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru,
	                       ModelStack* modelStack);
	void aftertouchReceived(MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                        bool* doingMidiThru, ModelStack* modelStack);

	// midi CC mappings
	int32_t getCCFromParam(deluge::modulation::params::Kind paramKind, int32_t paramID);

	int32_t paramToCC[kDisplayWidth][kDisplayHeight];
	int32_t previousKnobPos[kMaxMIDIValue + 1];
	uint32_t timeLastCCSent[kMaxMIDIValue + 1];
	uint32_t timeAutomationFeedbackLastSent;

private:
	// initialize
	void init();
	void initMapping(int32_t mapping[kDisplayWidth][kDisplayHeight]);

	// get model stack with auto param for midi follow cc-param control
	ModelStackWithAutoParam* getModelStackWithParamForSong(ModelStackWithThreeMainThings* modelStackWithThreeMainThings,
	                                                       int32_t xDisplay, int32_t yDisplay);
	ModelStackWithAutoParam* getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                       Clip* clip, int32_t xDisplay, int32_t yDisplay);
	ModelStackWithAutoParam*
	getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                   int32_t xDisplay, int32_t yDisplay);
	ModelStackWithAutoParam*
	getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                 int32_t xDisplay, int32_t yDisplay);
	ModelStackWithAutoParam*
	getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                   int32_t xDisplay, int32_t yDisplay);
	void displayParamControlError(int32_t xDisplay, int32_t yDisplay);

	// handle midi received for midi follow
	void offerReceivedNoteToKit(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
	                            int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                            bool* doingMidiThru, Clip* clip);

	void offerReceivedCCToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                          MIDIMatchType match, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                          bool* doingMidiThru, Clip* clip);

	void offerReceivedPitchBendToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 MIDIDevice* fromDevice, MIDIMatchType match, uint8_t channel, uint8_t data1,
	                                 uint8_t data2, bool* doingMidiThru, Clip* clip);

	void offerReceivedAftertouchToKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                  MIDIDevice* fromDevice, MIDIMatchType match, int32_t channel, int32_t value,
	                                  int32_t noteCode, bool* doingMidiThru, Clip* clip);

	MIDIMatchType checkMidiFollowMatch(MIDIDevice* fromDevice, uint8_t channel);
	bool isFeedbackEnabled();
	Drum* getDrumFromNoteCode(Clip* clip, Kit* kit, int32_t noteCode);

	// saving
	void writeDefaultsToFile();
	void writeDefaultMappingsToFile();

	// loading
	bool successfullyReadDefaultsFromFile;
	void readDefaultMappingsFromFile();
};

extern MidiFollow midiFollow;
