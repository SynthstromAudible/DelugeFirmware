/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "gui/ui/ui.h"
#include "hid/button.h"

#define SLICER_MODE_REGION 0
#define SLICER_MODE_MANUAL 1
#define MAX_MANUAL_SLICES 64

struct SliceItem {
	int32_t startPos;
	int32_t transpose;
};

class Slicer final : public UI {
public:
	Slicer() { oledShowsUIUnderneath = true; }

	void focusRegained();
	bool canSeeViewUnderneath() { return false; }
	void selectEncoderAction(int8_t offset);
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea);
	void graphicsRoutine();
	ActionResult horizontalEncoderAction(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);

	void stopAnyPreviewing();
	void preview(int64_t startPoint, int64_t endPoint, int32_t transpose, int32_t on);

	int32_t numManualSlice;
	int32_t currentSlice;
	int32_t slicerMode;
	SliceItem manualSlicePoints[MAX_MANUAL_SLICES];

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	int16_t numClips;

	// ui
	UIType getUIType() override { return UIType::SLICER; }

private:
	// 7SEG Only
	void redraw();

	void doSlice();
};

extern Slicer slicer;
