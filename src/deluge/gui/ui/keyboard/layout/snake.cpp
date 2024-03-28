/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/layout/snake.h"
#include "hid/display/display.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutSnake::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes

	// for each pressed pad, place or remove food
	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			if (snakeFood[presses[idxPress].x][presses[idxPress].y]) {
				snakeFood[presses[idxPress].x][presses[idxPress].y] = false; // FOOD OFF
				display->displayPopup("OFF");
			} else {
				snakeFood[presses[idxPress].x][presses[idxPress].y] = true; // FOOD ON
				display->displayPopup("ON");
			}
		}
	}
	snakeGreen.stepForward();
}

void KeyboardLayoutSnake::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	if (offset > 0) {
		snakeGreen.turnClockwise();
	} else if (offset < 0) {
		snakeGreen.turnCounterclockwise();
	}
}


void KeyboardLayoutSnake::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
}

void KeyboardLayoutSnake::precalculate() {
}

void KeyboardLayoutSnake::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			if (snakeGreen.x == x && snakeGreen.y == y) {
				image[y][x] = colours::green;
			} else if (snakeFood[x][y]) {
				image[y][x] = colours::white_full;
			} else {
				image[y][x] = colours::black;
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
