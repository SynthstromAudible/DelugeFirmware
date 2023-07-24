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

#pragma once

#include "gui/ui/keyboard/layout/isomorphic.h"
#include "definitions.h"
#include "util/functions.h"

namespace keyboard::layout {

KeyboardLayoutIsomorphic::KeyboardLayoutIsomorphic() : KeyboardLayout() {
}

void KeyboardLayoutIsomorphic::evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{};

	for (int idxPress = 0; idxPress < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutIsomorphic::handleVerticalEncoder(int offset) {
	handleHorizontalEncoder(offset * getState()->rowInterval, false);
}

void KeyboardLayoutIsomorphic::handleHorizontalEncoder(int offset, bool shiftEnabled) {
	if (shiftEnabled) {
		getState()->rowInterval += offset;
		if (getState()->rowInterval < 1) {
			getState()->rowInterval = 1;
		}
		else if (getState()->rowInterval > kMaxKeyboardRowInterval) {
			getState()->rowInterval = kMaxKeyboardRowInterval;
		}

		char buffer[13] = "row step:   ";
		intToString(getState()->rowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
		numericDriver.displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	volatile int test = getHighestClipNote();

	// Calculate highest possible displayable note with current rowInterval
	int highestScrolledNote =
	    (getHighestClipNote() - ((kDisplayHeight - 1) * getState()->rowInterval + (kDisplayWidth - 1)));

	// Make sure current value is in bounds
	getState()->scrollOffset = getMax(getLowestClipNote(), getState()->scrollOffset);
	getState()->scrollOffset = getMin(getState()->scrollOffset, highestScrolledNote);

	// Offset if still in bounds (check for verticalEncoder)
	int newOffset = getState()->scrollOffset + offset;
	if (newOffset >= getLowestClipNote() && newOffset <= highestScrolledNote) {
		getState()->scrollOffset = newOffset;
	}

	recalculate();
}

void KeyboardLayoutIsomorphic::recalculate() {
	// Pre-Buffer colours for next renderings
	for (int i = 0; i < kDisplayHeight * getState()->rowInterval + kDisplayWidth; ++i) {
		getNoteColour(getState()->scrollOffset + i, noteColours[i]);
	}
}

void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Precreate list of all active notes per octave
	bool octaveActiveNotes[12] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		octaveActiveNotes[(currentNotesState.notes[idx].note - getRootNote() + 132) % 12] = true;
	}

	// Precreate list of all scale notes per octave
	bool octaveScaleNotes[12] = {0};
	if (getScaleModeEnabled()) {
		uint8_t* scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes[scaleNotes[idx]] = true;
		}
	}

	// Iterate over grid image
	for (int y = 0; y < kDisplayHeight; ++y) {
		int noteCode = noteFromCoords(0, y);
		int yDisplay = noteCode - getState()->scrollOffset;
		int noteWithinOctave = (uint16_t)(noteCode - getRootNote() + 132) % (uint8_t)12;

		for (int x = 0; x < kDisplayWidth; x++) {
			// Full color for every octaves root and active notes
			if (octaveActiveNotes[noteWithinOctave] || noteWithinOctave == 0) {
				memcpy(image[y][x], noteColours[yDisplay], 3);
			}
			// Or, if this note is just within the current scale, show it dim
			else if (octaveScaleNotes[noteWithinOctave]) {
				getTailColour(image[y][x], noteColours[yDisplay]);
			}

			++noteCode;
			++yDisplay;
			noteWithinOctave = (noteWithinOctave + 1) % 12;
		}
	}
}

uint8_t KeyboardLayoutIsomorphic::noteFromCoords(int x, int y) {
	return getState()->scrollOffset + x + y * getState()->rowInterval;
}

} // namespace keyboard::layout
