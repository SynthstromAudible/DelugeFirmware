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
#include "gui/views/clip_navigation_timeline_view.h"
#include "hid/button.h"
#include "model/global_effectable/global_effectable.h"
#include "storage/flash_storage.h"

class Editor;
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

class MidiSessionView final : public ClipNavigationTimelineView, public GlobalEffectable {
public:
	MidiSessionView();
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

	//horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);

	//vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);

	//mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);

	//select encoder action
	void selectEncoderAction(int8_t offset);

	//not sure why we need these...
	uint32_t getMaxZoom();
	uint32_t getMaxLength();

	//midi CC mappings
	uint8_t paramToCC[kDisplayWidth][kDisplayHeight];
	MidiPadPress lastPadPress;
	uint8_t currentCC;
	bool onParamDisplay;
	bool showLearnedParams;

	ModelStackWithAutoParam* getModelStackWithParam(int32_t xDisplay, int32_t yDisplay);
	void learnCC(uint8_t channel, uint8_t ccNumber);

private:
	//initialize
	void initPadPress(MidiPadPress& padPress);
	void initParamToCC(uint8_t mapping[kDisplayWidth][kDisplayHeight]);

	//display
	void renderParamDisplay(Param::Kind paramKind, int32_t paramID, uint8_t ccNumber);

	//rendering
	void renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void setCentralLEDStates();

	//pad action
	void potentialShortcutPadAction(int32_t xDisplay, int32_t yDisplay);

	//learning
	void cantLearn(uint8_t channel);

	//change status
	void updateMappingChangeStatus();
	bool anyChangesToSave;

	// save/load default values
	uint8_t backupXMLParamToCC[kDisplayWidth][kDisplayHeight];

	//saving
	void saveMidiFollowMappings();
	void writeDefaultsToFile();
	void writeDefaultMappingsToFile();

	//loading
	bool successfullyReadDefaultsFromFile;
	void loadMidiFollowMappings();
	void readDefaultsFromBackedUpFile();
	void readDefaultsFromFile();
	void readDefaultMappingsFromFile();
};

extern MidiSessionView midiSessionView;
