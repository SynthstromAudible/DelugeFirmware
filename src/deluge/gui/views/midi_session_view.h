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
#include "gui/ui/root_ui.h"
#include "model/global_effectable/global_effectable.h"

class InstrumentClip;
class Clip;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;

struct MidiPadPress {
	bool isActive;
	int32_t xDisplay;
	int32_t yDisplay;
	Param::Kind paramKind;
	int32_t paramID;
};

class MidiSessionView final : public RootUI, public GlobalEffectable {
public:
	MidiSessionView();
	void readDefaultsFromFile();
	bool opened();
	void focusRegained();

	void graphicsRoutine();
	ActionResult timerCallback();

	//rendering
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderViewDisplay();
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
	// 7SEG only
	void redrawNumericDisplay();
	void setLedStates();

	//button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	//pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	//midi follow context
	Clip* getClipForMidiFollow(bool useActiveClip = false);
	Clip* clipForLastNoteReceived;
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
	void bendRangeUpdateReceived(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
	                             int32_t whichBendRange, int32_t bendSemitones);

	//midi CC mappings
	void learnCC(int32_t channel, int32_t ccNumber);
	int32_t getCCFromParam(Param::Kind paramKind, int32_t paramID);

	int32_t paramToCC[kDisplayWidth][kDisplayHeight];
	int32_t previousKnobPos[kDisplayWidth][kDisplayHeight];
	uint32_t timeLastCCSent[kMaxCCNumber + 1];
	uint32_t timeAutomationFeedbackLastSent;

private:
	//initialize
	void initView();
	void initPadPress(MidiPadPress& padPress);
	void initMapping(int32_t mapping[kDisplayWidth][kDisplayHeight]);

	//display
	void renderParamDisplay(Param::Kind paramKind, int32_t paramID, int32_t ccNumber);

	//rendering
	void renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void setCentralLEDStates();

	//pad action
	void potentialShortcutPadAction(int32_t xDisplay, int32_t yDisplay);

	//learning
	void cantLearn(int32_t channel);

	//change status
	void updateMappingChangeStatus();
	bool anyChangesToSave;

	// save/load default values
	int32_t backupXMLParamToCC[kDisplayWidth][kDisplayHeight];

	//saving
	void saveMidiFollowMappings();
	void writeDefaultsToFile();
	void writeDefaultMappingsToFile();

	//loading
	bool successfullyReadDefaultsFromFile;
	void loadMidiFollowMappings();
	void readDefaultsFromBackedUpFile();
	void readDefaultMappingsFromFile();

	//learning view related
	MidiPadPress lastPadPress;
	int32_t currentCC;
	bool onParamDisplay;
	bool showLearnedParams;
};

extern MidiSessionView midiSessionView;
