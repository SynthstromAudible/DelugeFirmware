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

namespace keyboard::layout {

void KeyboardLayoutVelocityDrums::evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int idxPress = 0; idxPress < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int offset) {
	handleHorizontalEncoder(offset * getState()->rowInterval, false);
}

void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int offset, bool shiftEnabled) {
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

void KeyboardLayoutVelocityDrums::recalculate() {
	// Pre-Buffer colours for next renderings
	for (int i = 0; i < kDisplayHeight * getState()->rowInterval + kDisplayWidth; ++i) {
		getNoteColour(getState()->scrollOffset + i, noteColours[i]);
	}
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

uint8_t KeyboardLayoutVelocityDrums::noteFromCoords(int x, int y) {
	return getState()->scrollOffset + x + y * getState()->rowInterval;
}

}; // namespace keyboard::layout

// for drum kit velocity calculation is:
// int velocityToSound = ((x % 4) * 8) + ((y % 4) * 32) + 7; //@TODO: Get velocity from note

//void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int offset) {
/*
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == InstrumentType::KIT) { //
			//@TODO: Implement new way of scrolling internally
			// instrumentClipView.verticalEncoderAction(offset * 4, inCardRoutine); //@TODO: Refactor
		}
		else {
			//@TODO: Refactor doScroll
			offset = offset * getCurrentClip()->keyboardState.rowInterval;

			//@TODO: Refactor
			// Check we're not scrolling out of range // @TODO: Move this check into layout
			int newYNote;
			if (offset >= 0) {
				newYNote = getCurrentClip()->keyboardState.scrollOffset + (kDisplayHeight - 1) * getCurrentClip()->keyboardState.rowInterval + kDisplayWidth - 1;
			}
			else {
				newYNote = getCurrentClip()->keyboardState.scrollOffset;
			}

			if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
				return; // Nothing is updated if we are at ehe end of the displayable range
			}

			getCurrentClip()->keyboardState.scrollOffset += offset; //@TODO: Move keyboardState.scrollOffset into the layouts
		}
		*/
//}

///void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int offset, bool shiftEnabled) {
/*
	if(shiftEnabled) {
		InstrumentClip* clip = getCurrentClip();
		clip->keyboardState.rowInterval += offset;
		if (clip->keyboardState.rowInterval < 1) {
			clip->keyboardState.rowInterval = 1;
		}
		else if (clip->keyboardState.rowInterval > kMaxKeyboardRowInterval) {
			clip->keyboardState.rowInterval = kMaxKeyboardRowInterval;
		}

		char buffer[13] = "row step:   ";
		intToString(clip->keyboardState.rowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
		numericDriver.displayPopup(buffer);
		doScroll(0, true);
		return;
	}

	if (instrument->type == InstrumentType::KIT) {
		handleVerticalEncoder(offset);
	}
	else {
		//@TODO: Refactor
		// Check we're not scrolling out of range // @TODO: Move this check into layout
		int newYNote;
		if (offset >= 0) {
			newYNote = getCurrentClip()->keyboardState.scrollOffset + (kDisplayHeight - 1) * getCurrentClip()->keyboardState.rowInterval + kDisplayWidth - 1;
		}
		else {
			newYNote = getCurrentClip()->keyboardState.scrollOffset;
		}
		if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
			return; // Nothing is updated if we are at ehe end of the displayable range
		}

		getCurrentClip()->keyboardState.scrollOffset += offset; //@TODO: Move keyboardState.scrollOffset into the layouts
	}
	*/
//}

// void KeyboardLayoutVelocityDrums::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) { //@TODO: Refactor

// 	// Calculate colors
// 	uint8_t noteColours[kDisplayHeight * kMaxKeyboardRowInterval + kDisplayWidth][3];
// 	InstrumentClip* clip = getCurrentClip();
// 	for (int i = 0; i < kDisplayHeight * clip->keyboardState.rowInterval + kDisplayWidth; i++) { // @TODO: find out how to do without dependency
// 		clip->getMainColourFromY(clip->keyboardState.scrollOffset + i, 0, noteColours[i]);
// 	}

// 	// First, piece together a picture of all notes-within-an-octave which are active
// 	bool notesWithinOctaveActive[12];
// 	memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
// 	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
// 		int noteWithinOctave = (currentNotesState.notes[idx].note - getRootNote() + 132) % 12;
// 		notesWithinOctaveActive[noteWithinOctave] = true;
// 	}

// 	uint8_t noteColour[] = {127, 127, 127};

// 	for (int y = 0; y < kDisplayHeight; y++) {
// 		int noteCode = noteFromCoords(0, y);
// 		int yDisplay = noteCode - ((InstrumentClip*)currentSong->currentClip)->keyboardState.scrollOffset;
// 		int noteWithinOctave = (uint16_t)(noteCode - currentSong->rootNote + 132) % (uint8_t)12;

// 		for (int x = 0; x < kDisplayWidth; x++) {

// 			// If auditioning note with finger - or same note in different octave...
// 			if (notesWithinOctaveActive[noteWithinOctave]) {
// 	doFullColour:
// 				memcpy(image[y][x], noteColours[yDisplay], 3);
// 			}
// 			// Show root note within each octave as full colour
// 			else if (!noteWithinOctave) {
// 				goto doFullColour;

// 				// Or, if this note is just within the current scale, show it dim
// 			}
// 			else {
// 				if (((InstrumentClip*)currentSong->currentClip)->inScaleMode && currentSong->modeContainsYNote(noteCode)) {
// 					getTailColour(image[y][x], noteColours[yDisplay]);
// 				}
// 			}

// 			// @TODO: Migrate to drum layout
// 			// if (instrument->type == InstrumentType::KIT) {
// 			// 	int myV = ((x % 4) * 16) + ((y % 4) * 64) + 8;
// 			// 	int myY = (int)(x / 4) + (int)(y / 4) * 4;

// 			// 	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
// 			// 		((InstrumentClip*)currentSong->currentClip)->getNoteRowOnScreen(myY, modelStackWithTimelineCounter);

// 			// 	if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

// 			// 		instrumentClipView.getRowColour(myY, noteColour);

// 			// 		noteColour[0] = (char)((noteColour[0] * myV / 255) / 3);
// 			// 		noteColour[1] = (char)((noteColour[1] * myV / 255) / 3);
// 			// 		noteColour[2] = (char)((noteColour[2] * myV / 255) / 3);
// 			// 	}
// 			// 	else {
// 			// 		noteColour[0] = 2;
// 			// 		noteColour[1] = 2;
// 			// 		noteColour[2] = 2;
// 			// 	}
// 			// 	memcpy(image[y][x], noteColour, 3);
// 			// }
// 			// Otherwise, square will just get left black, from its having been wiped above

// 			// Dim note pad if a browser is open with the note highlighted //@TODO: Add API for highlighted notes
// 			// if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
// 			// 	|| (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
// 			// 	if (soundEditor.isUntransposedNoteWithinRange(noteCode)) {
// 			// 		for (int colour = 0; colour < 3; colour++) {
// 			// 			int value = (int)image[y][x][colour] + 35;
// 			// 			image[y][x][colour] = getMin(value, 255);
// 			// 		}
// 			// 	}
// 			// }

// 			noteCode++;
// 			yDisplay++;
// 			noteWithinOctave++;
// 			if (noteWithinOctave == 12) {
// 				noteWithinOctave = 0;
// 			}
// 		}
// 	}
// }
