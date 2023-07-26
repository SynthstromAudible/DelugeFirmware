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

#include "gui/ui/keyboard/layout/in_key.h"
#include "definitions.h"
#include "util/functions.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"

namespace keyboard::layout {

void KeyboardLayoutInKey::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutInKey::handleVerticalEncoder(int offset) {
	handleHorizontalEncoder(offset * getState().inKey.rowInterval, false);
}

void KeyboardLayoutInKey::handleHorizontalEncoder(int offset, bool shiftEnabled) {
	KeyboardStateInKey& state = getState().inKey;

	if (shiftEnabled) {
		state.rowInterval += offset;
		state.rowInterval = getMax(state.rowInterval, kMinInKeyRowInterval);
		state.rowInterval = getMin(kMaxInKeyRowInterval, state.rowInterval);

		char buffer[13] = "Row step:   ";
		intToString(state.rowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
		numericDriver.displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	// Calculate highest and lowest possible displayable note with current rowInterval
	int lowestScrolledNote = padIndexFromNote(getLowestClipNote());
	int highestScrolledNote = (padIndexFromNote(getHighestClipNote())
	                           - ((kDisplayHeight * state.rowInterval + kDisplayWidth) - state.rowInterval - 1));

	// Make sure current value is in bounds
	state.scrollOffset = getMax(lowestScrolledNote, state.scrollOffset);
	state.scrollOffset = getMin(state.scrollOffset, highestScrolledNote);

	// Offset if still in bounds (check for verticalEncoder)
	int newOffset = state.scrollOffset + offset;
	if (newOffset >= lowestScrolledNote && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutInKey::precalculate() {
	KeyboardStateInKey& state = getState().inKey;

	// Pre-Buffer colours for next renderings
	for (int i = 0; i < kDisplayHeight * state.rowInterval + kDisplayWidth; ++i) {
		getNoteColour(noteFromPadIndex(state.scrollOffset + i), noteColours[i]);
	}
}

void KeyboardLayoutInKey::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Precreate list of all active notes per octave
	bool scaleActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		scaleActiveNotes[((currentNotesState.notes[idx].note - getRootNote()) + kOctaveSize) % kOctaveSize] = true;
	}

	uint8_t scaleNoteCount = getScaleNoteCount();

	// Iterate over grid image
	for (int y = 0; y < kDisplayHeight; ++y) {
		for (int x = 0; x < kDisplayWidth; x++) {
			int noteCode = noteFromCoords(x, y);
			//int normalizedPadOffset = noteCode - getState().isomorphic.scrollOffset;
			int noteWithinScale = (uint16_t)((noteCode - getRootNote()) + kOctaveSize) % kOctaveSize;

			// Full color for every octaves root and active notes
			if (noteWithinScale == 0) {
				//memcpy(image[y][x], noteColours[normalizedPadOffset], 3);
				memset(image[y][x], 0xFF, 3);
			}
			// // Or, if this note is just within the current scale, show it dim // @TODO: Check calculation
			// else if (scaleActiveNotes[noteCode]) {
			// 	getTailColour(image[y][x], noteColours[normalizedPadOffset]);
			// }
			else {
				// Color other scale notes dimly white
				memset(image[y][x], 0x05, 3);
			}
		}
	}
}

} // namespace keyboard::layout
