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

// Clip Group colours
extern const uint8_t numDefaultClipGroupColours;
extern const uint8_t defaultClipGroupColours[];

class PerformanceSessionView final : public ClipNavigationTimelineView, public ModControllableAudio {
public:
	PerformanceSessionView();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	bool opened();
	void focusRegained();

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	uint32_t getMaxZoom();
	uint32_t getMaxLength();
	bool renderRow(ModelStack* modelStack, uint8_t yDisplay, uint8_t thisImage[kDisplayWidth + kSideBarWidth][3],
	               uint8_t thisOccupancyMask[kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void graphicsRoutine();
	void requestRendering(UI* ui, uint32_t whichMainRows = 0xFFFFFFFF, uint32_t whichSideRows = 0xFFFFFFFF);

	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void selectEncoderAction(int8_t offset);
	ActionResult timerCallback();
	void setLedStates();
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	// 7SEG only
	void redrawNumericDisplay();
	void renderDisplay();

	uint32_t selectedClipTimePressed;
	uint8_t selectedClipYDisplay;      // Where the clip is on screen
	uint8_t selectedClipPressYDisplay; // Where the user's finger actually is on screen
	uint8_t selectedClipPressXDisplay;
	bool performActionOnPadRelease;
	bool
	    performActionOnSectionPadRelease; // Keep this separate from the above one because we don't want a mod encoder action to set this to false
	uint8_t sectionPressed;
	uint8_t masterCompEditMode;

private:
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	void renderRow(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);

	void setCentralLEDStates();
	ModelStackWithAutoParam* getModelStackWithParam(int32_t paramID);
	int32_t calculateKnobPosForSinglePadPress(int32_t yDisplay);
	int32_t calculateKnobPosForDisplay(int32_t knobPos);
	int32_t getParamIDFromSinglePadPress(int32_t xDisplay);
	void renderDisplayOLED(Param::Kind lastSelectedParamKind, int32_t lastSelectedParamID, int32_t knobPos);
	int32_t currentKnobPosition[kDisplayWidth];
	int32_t previousKnobPosition[kDisplayWidth];
	int32_t previousPadPressYDisplay[kDisplayWidth];
	uint32_t timeLastPadPress[kDisplayWidth];
	bool padPressHeld[kDisplayWidth];

	// Members regarding rendering different layouts
private:
	bool sessionButtonActive = false;
	bool sessionButtonUsed = false;
};

extern PerformanceSessionView performanceSessionView;
