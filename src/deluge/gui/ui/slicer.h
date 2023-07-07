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
#include "hid/display.h"

class Slicer final : public UI {
public:
	Slicer() { oledShowsUIUnderneath = display.type == DisplayType::OLED; }

	void focusRegained();
	bool canSeeViewUnderneath() { return true; }
	void selectEncoderAction(int8_t offset);
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	int padAction(int x, int y, int velocity);

	// OLED only
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	int16_t numClips;

private:
	// 7SEG Only
	void redraw();

	void doSlice();
};

extern Slicer slicer;
