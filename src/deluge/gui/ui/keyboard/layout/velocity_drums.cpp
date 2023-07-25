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

#include "gui/ui/keyboard/layout/velocity_drums.h"
#include "definitions.h"
#include "util/functions.h"
#include "model/model_stack.h"
#include "gui/views/instrument_clip_view.h"

namespace keyboard::layout {

void KeyboardLayoutVelocityDrums::evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int idxPress = 0; idxPress < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             ((presses[idxPress].x % 4) * 8) + ((presses[idxPress].y % 4) * 32) + 7);
		}
	}
}

void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int offset) {
	//@TODO: Implement new way of scrolling internally
	instrumentClipView.verticalEncoderAction(offset * 4, false); //@TODO: Refactor
	recalculate();
}

void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int offset, bool shiftEnabled) {
	handleVerticalEncoder(offset);
}

void KeyboardLayoutVelocityDrums::recalculate() {
	//@TODO: Refactor
	// // Pre-Buffer colours for next renderings
	// for (int i = 0; i < kDisplayHeight * getState()->isomorphic.rowInterval + kDisplayWidth; ++i) {
	// 	getNoteColour(getState()->isomorphic.scrollOffset + i, noteColours[i]);
	// }
}

void KeyboardLayoutVelocityDrums::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
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

	uint8_t noteColour[] = {127, 127, 127};
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(currentSong->currentClip);

	// Iterate over grid image
	for (int y = 0; y < kDisplayHeight; ++y) {
		int noteCode = noteFromCoords(0, y);
		int yDisplay = noteCode - getState()->isomorphic.scrollOffset;
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

			int myV = ((x % 4) * 16) + ((y % 4) * 64) + 8;
			int myY = (int)(x / 4) + (int)(y / 4) * 4;

			//@TODO: Refactor
			ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
			    currentClip()->getNoteRowOnScreen(myY, modelStackWithTimelineCounter);

			if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

				instrumentClipView.getRowColour(myY, noteColour);

				noteColour[0] = (char)((noteColour[0] * myV / 255) / 3);
				noteColour[1] = (char)((noteColour[1] * myV / 255) / 3);
				noteColour[2] = (char)((noteColour[2] * myV / 255) / 3);
			}
			else {
				noteColour[0] = 2;
				noteColour[1] = 2;
				noteColour[2] = 2;
			}
			memcpy(image[y][x], noteColour, 3);

			++noteCode;
			++yDisplay;
			noteWithinOctave = (noteWithinOctave + 1) % 12;
		}
	}
}

uint8_t KeyboardLayoutVelocityDrums::noteFromCoords(int x, int y) {
	return (int)(x / 4) + (int)(y / 4) * 4;
}

}; // namespace keyboard::layout
