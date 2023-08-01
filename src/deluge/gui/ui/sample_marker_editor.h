/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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
#include "gui/ui/ui.h"
#include "RZA1/system/r_typedefs.h"
#include "hid/button.h"

class Sample;
class MultisampleRange;
struct MarkerColumn;

// This is just for when we're editing a Sample's loop points etc. It mostly makes use of WaveformBasicNavigator,
// which itself makes use of WaveformRenderer. However, I've implemented this without any class inheritance

class SampleMarkerEditor final : public UI {
public:
	SampleMarkerEditor();

	bool opened();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	void selectEncoderAction(int8_t offset);
	ActionResult padAction(int x, int y, int velocity);
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine);
	ActionResult verticalEncoderAction(int offset, bool inCardRoutine);
	ActionResult horizontalEncoderAction(int offset);
	void graphicsRoutine();
	ActionResult timerCallback();
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3] = NULL,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL, bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3] = NULL,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = NULL);

	// OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	// 7SEG
	void displayText();

	MarkerType markerType;

	bool blinkInvisible;

	int8_t pressX;
	int8_t pressY;

private:
	void writeValue(uint32_t value, MarkerType markerTypeNow = MarkerType::NOT_AVAILABLE);
	void exitUI();

	int getStartColOnScreen(int32_t unscrolledPos);
	int getEndColOnScreen(int32_t unscrolledPos);
	int getStartPosFromCol(int col);
	int getEndPosFromCol(int col);
	void getColsOnScreen(MarkerColumn* cols);
	void recordScrollAndZoom();
	bool shouldAllowExtraScrollRight();
	void renderForOneCol(int xDisplay, uint8_t thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth][3],
	                     MarkerColumn* cols);
	void renderMarkersForOneCol(int xDisplay, uint8_t thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth][3],
	                            MarkerColumn* cols);
};

extern SampleMarkerEditor sampleMarkerEditor;
