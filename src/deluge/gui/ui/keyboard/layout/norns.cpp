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

#include "gui/ui/keyboard/layout/norns.h"

namespace deluge::gui::ui::keyboard::layout {

PLACE_SDRAM_TEXT void KeyboardLayoutNorns::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutNorns::handleVerticalEncoder(int32_t offset) {
}

PLACE_SDRAM_TEXT void KeyboardLayoutNorns::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                   PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                   bool encoderPressed) {
}

PLACE_SDRAM_TEXT void KeyboardLayoutNorns::precalculate() {
}

PLACE_SDRAM_TEXT void KeyboardLayoutNorns::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t note = noteFromCoords(x, y);

			// If norns is active, do it
			if (getNornsNotes()[note] != 0) {
				image[y][x] = colours::white_full.adjust(getNornsNotes()[note], 1);
			}
			else {
				image[y][x] = colours::black;
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
