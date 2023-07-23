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

#include "model/instrument/instrument.h"
#include "model/clip/instrument_clip.h"

namespace keyboard::layout {

// Refactor remove area

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}

//-----------------------

KeyboardLayoutIsomorphic::KeyboardLayoutIsomorphic() : KeyboardLayout() {
}

void KeyboardLayoutIsomorphic::evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{};

	for (int idxPress = 0; idxPress < MAX_NUM_KEYBOARD_PAD_PRESSES; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getActiveInstrument()->defaultVelocity);
		}
	}
}

void KeyboardLayoutIsomorphic::handleVerticalEncoder(int offset) {
	offset = offset * getCurrentClip()->keyboardRowInterval;

	int newYNote;
	if (offset >= 0) {
		newYNote = getCurrentClip()->yScrollKeyboardScreen
		           + (kDisplayHeight - 1) * getCurrentClip()->keyboardRowInterval + kDisplayWidth - 1;
	}
	else {
		newYNote = getCurrentClip()->yScrollKeyboardScreen;
	}

	//@TODO: replicate this check
	// if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
	// 	return; // Nothing is updated if we are at ehe end of the displayable range
	// }

	getCurrentClip()->yScrollKeyboardScreen += offset; //@TODO: Move yScrollKeyboardScreen into the layouts
}

void KeyboardLayoutIsomorphic::handleHorizontalEncoder(int offset, bool shiftEnabled) {
	if (shiftEnabled) {
		InstrumentClip* clip = getCurrentClip();
		clip->keyboardRowInterval += offset;
		if (clip->keyboardRowInterval < 1) {
			clip->keyboardRowInterval = 1;
		}
		else if (clip->keyboardRowInterval > kMaxKeyboardRowInterval) {
			clip->keyboardRowInterval = kMaxKeyboardRowInterval;
		}

		char buffer[13] = "row step:   ";
		intToString(clip->keyboardRowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
		numericDriver.displayPopup(buffer);
		return;
	}

	int newYNote;
	if (offset >= 0) {
		newYNote = getCurrentClip()->yScrollKeyboardScreen
		           + (kDisplayHeight - 1) * getCurrentClip()->keyboardRowInterval + kDisplayWidth - 1;
	}
	else {
		newYNote = getCurrentClip()->yScrollKeyboardScreen;
	}

	//@TODO: replicate this check
	// if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
	// 	return; // Nothing is updated if we are at ehe end of the displayable range
	// }

	getCurrentClip()->yScrollKeyboardScreen += offset; //@TODO: Move yScrollKeyboardScreen into the layouts
}

void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) { //@TODO: Refactor

	// Calculate colors
	uint8_t noteColours[kDisplayHeight * kMaxKeyboardRowInterval + kDisplayWidth][3];
	InstrumentClip* clip = getCurrentClip();
	for (int i = 0; i < kDisplayHeight * clip->keyboardRowInterval + kDisplayWidth;
	     i++) { // @TODO: find out how to do without dependency
		clip->getMainColourFromY(clip->yScrollKeyboardScreen + i, 0, noteColours[i]);
	}

	// First, piece together a picture of all notes-within-an-octave which are active
	bool notesWithinOctaveActive[12];
	memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		int noteWithinOctave = (currentNotesState.notes[idx].note - getRootNote() + 120) % 12;
		notesWithinOctaveActive[noteWithinOctave] = true;
	}

	uint8_t noteColour[] = {127, 127, 127};

	for (int y = 0; y < kDisplayHeight; y++) {
		int noteCode = noteFromCoords(0, y);
		int yDisplay = noteCode - ((InstrumentClip*)currentSong->currentClip)->yScrollKeyboardScreen;
		int noteWithinOctave = (uint16_t)(noteCode - currentSong->rootNote + 120) % (uint8_t)12;

		for (int x = 0; x < kDisplayWidth; x++) {

			// If auditioning note with finger - or same note in different octave...
			if (notesWithinOctaveActive[noteWithinOctave]) {
doFullColour:
				memcpy(image[y][x], noteColours[yDisplay], 3);
			}
			// Show root note within each octave as full colour
			else if (!noteWithinOctave) {
				goto doFullColour;

				// Or, if this note is just within the current scale, show it dim
			}
			else {
				if (((InstrumentClip*)currentSong->currentClip)->inScaleMode
				    && currentSong->modeContainsYNote(noteCode)) {
					getTailColour(image[y][x], noteColours[yDisplay]);
				}
			}

			noteCode++;
			yDisplay++;
			noteWithinOctave++;
			if (noteWithinOctave == 12) {
				noteWithinOctave = 0;
			}
		}
	}
}

uint8_t KeyboardLayoutIsomorphic::noteFromCoords(int x, int y) {
	InstrumentClip* clip = (InstrumentClip*)currentSong->currentClip;
	return clip->yScrollKeyboardScreen + x + y * clip->keyboardRowInterval;
}

} // namespace keyboard::layout
