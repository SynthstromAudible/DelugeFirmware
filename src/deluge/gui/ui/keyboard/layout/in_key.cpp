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
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "util/functions.h"

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
	int highestScrolledNote =
	    (padIndexFromNote(getHighestClipNote()) - ((kDisplayHeight - 1) * state.rowInterval + kDisplayWidth - 1));

	// Make sure current value is in bounds
	state.scrollOffset = getMax(lowestScrolledNote, state.scrollOffset);
	state.scrollOffset = getMin(state.scrollOffset, highestScrolledNote);

	// Offset if still in bounds (reject if the next row can not be shown completely)
	int newOffset = state.scrollOffset + offset;
	if (newOffset >= lowestScrolledNote && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutInKey::precalculate() {
	KeyboardStateInKey& state = getState().inKey;

	// Pre-Buffer colours for next renderings
	for (int i = 0; i < (kDisplayHeight * state.rowInterval + kDisplayWidth); ++i) {
		getNoteColour(noteFromPadIndex(state.scrollOffset + i), noteColours[i]);
	}
}

inline void colorCopy(uint8_t* dest, uint8_t* src, uint8_t intensity, uint8_t brightnessDivider) {
	dest[0] = (uint8_t)((src[0] * intensity / 255) / brightnessDivider);
	dest[1] = (uint8_t)((src[1] * intensity / 255) / brightnessDivider);
	dest[2] = (uint8_t)((src[2] * intensity / 255) / brightnessDivider);
}

void KeyboardLayoutInKey::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Precreate list of all active notes per octave
	bool scaleActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		scaleActiveNotes[((currentNotesState.notes[idx].note + kOctaveSize) - getRootNote()) % kOctaveSize] = true;
	}

	uint8_t scaleNoteCount = getScaleNoteCount();

	// Iterate over grid image
	for (int y = 0; y < kDisplayHeight; ++y) {
		for (int x = 0; x < kDisplayWidth; x++) {
			auto padIndex = padIndexFromCoords(x, y);
			auto note = noteFromPadIndex(padIndex);
			int noteWithinScale = (uint16_t)((note + kOctaveSize) - getRootNote()) % kOctaveSize;
			uint8_t* colourSource = noteColours[padIndex - getState().inKey.scrollOffset];

			// Full brightness and color for active root note
			if (noteWithinScale == 0 && scaleActiveNotes[noteWithinScale]) {
				colorCopy(image[y][x], colourSource, 255, 1);
			}
			// Full color but less brightness for inactive root note
			else if (noteWithinScale == 0) {
				colorCopy(image[y][x], colourSource, 255, 2);
			}
			// TOned down color but high brightness for active scale note
			else if (scaleActiveNotes[noteWithinScale]) {
				colorCopy(image[y][x], colourSource, 127, 3);
			}
			// Dimly white for inactive scale notes
			else {
				memset(image[y][x], 1, 3);
			}
		}
	}
}

} // namespace keyboard::layout
