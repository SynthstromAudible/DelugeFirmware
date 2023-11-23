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

class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;

struct SnakeCoord {
	int32_t xDisplay;
	int32_t yDisplay;
};

class SnakeView final : public ClipNavigationTimelineView {
public:
	SnakeView();
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
	void setCentralLEDStates();

	SnakeCoord snakeHead;
	SnakeCoord snakeTail;

	int32_t snakeGrid[kDisplayWidth][kDisplayHeight];
	int32_t snakeDirection; //0 = left, 1 = right, 2 = up, 3 = down
	int32_t rowTickSquarePrevious;
	int32_t rowTickOffset;
	bool snakeDied;
};

extern SnakeView snakeView;
