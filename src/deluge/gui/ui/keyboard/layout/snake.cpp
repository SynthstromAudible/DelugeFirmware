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

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutSnake::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	// these will place food on grid
	currentNotesState = NotesState{}; // Erase active notes

	// loops through all pressed notes (max 10) then enables the note from the coordinates
	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			// NotesStates in notes_state.h has enableNote and noteEnabled and stuff
			// enableNote
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutSnake::handleVerticalEncoder(int32_t offset) {
}

void KeyboardLayoutSnake::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
}

void KeyboardLayoutSnake::precalculate() {
}

void KeyboardLayoutSnake::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// noteFromCoords defined in snake.h, returns midi note x + y * kDisplayWidth
			int32_t note = noteFromCoords(x, y);
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
