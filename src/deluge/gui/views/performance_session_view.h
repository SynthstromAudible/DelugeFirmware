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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "storage/flash_storage.h"

class Editor;
class InstrumentClip;
class Clip;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;

struct LastPadPress {
	bool isActive;
	int32_t yDisplay;
	int32_t xDisplay;
	Param::Kind paramKind;
	int32_t paramID;
};

class PerformanceSessionView final : public ClipNavigationTimelineView, public ModControllableAudio {
public:
	PerformanceSessionView();
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

private:
	//rendering
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	void renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void renderFXDisplay(Param::Kind paramKind, int32_t paramID, int32_t knobPos);
	void setCentralLEDStates();

	//pad action
	bool setParameterValue(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind, int32_t paramID,
	                       int32_t xDisplay, int32_t knobPos, bool renderDisplay = true);
	void padPressAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind, int32_t paramID,
	                    int32_t xDisplay, int32_t yDisplay, bool renderDisplay = true);
	void padReleaseAction(ModelStackWithThreeMainThings* modelStack, Param::Kind paramKind, int32_t paramID,
	                      int32_t xDisplay, bool renderDisplay = true);
	void resetPerformanceView(ModelStackWithThreeMainThings* modelStack);
	void releaseStutter(ModelStackWithThreeMainThings* modelStack);

	//write/load default values
	void writeDefaultsToFile();
	void writeDefaultFXValuesToFile();
	void writeDefaultFXRowValuesToFile(int32_t xDisplay);
	void readDefaultsFromFile();
	void readDefaultFXValuesFromFile();
	void readDefaultFXRowValuesFromFile(int32_t xDisplay);
	void readDefaultFXRowNumberValuesFromFile(int32_t xDisplay);
	bool successfullyReadDefaultsFromFile;
	bool anyChangesToSave;

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID);
	int32_t calculateKnobPosForSinglePadPress(int32_t yDisplay);
	int32_t calculateKnobPosForDisplay(int32_t knobPos);

	int32_t currentKnobPosition[kDisplayWidth];
	int32_t previousKnobPosition[kDisplayWidth];
	int32_t previousPadPressYDisplay[kDisplayWidth];
	uint32_t timeLastPadPress[kDisplayWidth];
	bool padPressHeld[kDisplayWidth];
	int32_t defaultFXValues[kDisplayWidth][kDisplayHeight];
	bool defaultEditingMode;
	LastPadPress lastPadPress;

	// Members regarding rendering different layouts
private:
	bool sessionButtonActive = false;
	bool sessionButtonUsed = false;
};

extern PerformanceSessionView performanceSessionView;
