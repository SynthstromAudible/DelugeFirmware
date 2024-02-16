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
	bool opened() override;
	void focusRegained() override;

	void graphicsRoutine() override;
	ActionResult timerCallback() override;

	// rendering
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderViewDisplay();
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) override;
	// 7SEG only
	void redrawNumericDisplay();
	void setLedStates();

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset) override;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	void modButtonAction(uint8_t whichButton, bool on) override;

	// select encoder action
	void selectEncoderAction(int8_t offset) override;

	// not sure why we need these...
	uint32_t getMaxZoom() override;
	uint32_t getMaxLength() override;

private:
	// rendering
	void renderRow(RGB* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void setCentralLEDStates();

	SnakeCoord snakeHead;
	SnakeCoord snakeTail;

	int32_t snakeGrid[kDisplayWidth][kDisplayHeight];
	int32_t snakeDirection; // 0 = left, 1 = right, 2 = up, 3 = down
	int32_t rowTickSquarePrevious;
	int32_t rowTickOffset;
	bool snakeDied;
};

extern SnakeView snakeView;
