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
	int startPos;
	int transpose;
};

class Slicer final : public UI {
public:
	Slicer();

	void focusRegained();
	bool canSeeViewUnderneath() { return false; }
	void selectEncoderAction(int8_t offset);
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	int padAction(int x, int y, int velocity);

	bool renderMainPads(uint32_t whichRows, uint8_t image[][displayWidth + sideBarWidth][3],
	                    uint8_t occupancyMask[][displayWidth + sideBarWidth], bool drawUndefinedArea);
	void graphicsRoutine();
	int horizontalEncoderAction(int offset);
	int verticalEncoderAction(int offset, bool inCardRoutine);

	void stopAnyPreviewing();
	void preview(int64_t startPoint, int64_t endPoint, int transpose, int on);

	int numManualSlice;
	int currentSlice;
	int slicerMode;
	SliceItem manualSlicePoints[MAX_MANUAL_SLICES];

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif
	int16_t numClips;

private:
#if !HAVE_OLED
	void redraw();
#endif
	void doSlice();
};

extern Slicer slicer;
