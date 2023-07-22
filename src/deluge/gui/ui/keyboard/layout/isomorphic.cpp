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
bool yDisplayActive[kDisplayHeight * kMaxKeyboardRowInterval + kDisplayWidth]; // @TODO: needs to be emptied at start

inline InstrumentClip* getCurrentClip() {
	return (InstrumentClip*)currentSong->currentClip;
}
//-----------------------

KeyboardLayoutIsomorphic::KeyboardLayoutIsomorphic() : KeyboardLayout() {
	memset(yDisplayActive, 0, sizeof(yDisplayActive));
}

void KeyboardLayoutIsomorphic::evaluatePads(PressedPad presses[MAX_NUM_KEYBOARD_PAD_PRESSES]) {
	/*


	int noteCode = noteFromCoords(x, y);

	// Press-down
	if (velocity) {
		if(notesFull()) {
			return;
		}

		int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == InstrumentType::KIT) { //
			yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
		}
		if (yDisplayActive[yDisplay]) { // Exit if this pad was already pressed
			return;
		}

		// Only now that we know we're not going to return prematurely can we mark the pad as pressed
		{
			padPresses[emptyPressIndex].x = x;
			padPresses[emptyPressIndex].y = y;
			yDisplayActive[yDisplay] = true;
		}

		//@TODO: Add note
	}

	// Press-up
	else {

		int p;
		for (p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
			if (padPresses[p].x == x && padPresses[p].y == y) {
				goto foundIt;
			}
		}


		return;

foundIt:
		padPresses[p].x = 255; //255 seems to be reset
		noteCode = noteFromCoords(x, y);
		int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == InstrumentType::KIT) { // @TODO: Deduplicate
			yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
		}

		// We need to check that we had actually switched the note on here - it might have already been sounding, from the sequence
		if (!yDisplayActive[yDisplay]) {
			return;
		}


		// If we had indeed sounded the note via audition (as opposed to it being on in the sequence), switch it off.
		// If that was not the case, well, we still did want to potentially exit audition mode above, cos users been reporting stuck note problems,
		// even though I can't see quite how we'd get stuck there
		if (yDisplayActive[yDisplay]) {

			//@TODO: Remove note


			yDisplayActive[yDisplay] = false;
		}


	}
	*/
}

void KeyboardLayoutIsomorphic::handleVerticalEncoder(int offset) {
	/*
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == InstrumentType::KIT) { //
			//@TODO: Implement new way of scrolling internally
			// instrumentClipView.verticalEncoderAction(offset * 4, inCardRoutine); //@TODO: Refactor
		}
		else {
			//@TODO: Refactor doScroll
			offset = offset * getCurrentClip()->keyboardRowInterval;

			//@TODO: Refactor
			// Check we're not scrolling out of range // @TODO: Move this check into layout
			int newYNote;
			if (offset >= 0) {
				newYNote = getCurrentClip()->yScrollKeyboardScreen + (kDisplayHeight - 1) * getCurrentClip()->keyboardRowInterval + kDisplayWidth - 1;
			}
			else {
				newYNote = getCurrentClip()->yScrollKeyboardScreen;
			}

			if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
				return; // Nothing is updated if we are at ehe end of the displayable range
			}

			getCurrentClip()->yScrollKeyboardScreen += offset; //@TODO: Move yScrollKeyboardScreen into the layouts
		}
		*/
}

void KeyboardLayoutIsomorphic::handleHorizontalEncoder(int offset, bool shiftEnabled) {
	/*
	if(shiftEnabled) {
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
			newYNote = getCurrentClip()->yScrollKeyboardScreen + (kDisplayHeight - 1) * getCurrentClip()->keyboardRowInterval + kDisplayWidth - 1;
		}
		else {
			newYNote = getCurrentClip()->yScrollKeyboardScreen;
		}
		if (!force && !getCurrentClip()->isScrollWithinRange(offset, newYNote + offset)) {
			return; // Nothing is updated if we are at ehe end of the displayable range
		}

		getCurrentClip()->yScrollKeyboardScreen += offset; //@TODO: Move yScrollKeyboardScreen into the layouts
	}
	*/
}

uint8_t noteColours[kDisplayHeight * kMaxKeyboardRowInterval + kDisplayWidth][3];

void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) { //@TODO: Refactor
	                                                                                           /*
	// Calculate colors
	InstrumentClip* clip = getCurrentClip();
	for (int i = 0; i < kDisplayHeight * clip->keyboardRowInterval + kDisplayWidth; i++) { // @TODO: find out how to do without dependency
		clip->getMainColourFromY(clip->yScrollKeyboardScreen + i, 0, noteColours[i]);
	}


	// First, piece together a picture of all notes-within-an-octave which are active
	bool notesWithinOctaveActive[12];
	memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
	for(const auto& note : activeNotes) {
		int noteWithinOctave = (note - getRootNote() + 120) % 12;
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
				memcpy(image[y][x], keyboardScreen.noteColours[yDisplay], 3);
			}
			// Show root note within each octave as full colour
			else if (!noteWithinOctave) {
				goto doFullColour;

				// Or, if this note is just within the current scale, show it dim
			}
			else {
				if (((InstrumentClip*)currentSong->currentClip)->inScaleMode && currentSong->modeContainsYNote(noteCode)) {
					getTailColour(image[y][x], keyboardScreen.noteColours[yDisplay]);
				}
			}

			// @TODO: Migrate to drum layout
			// if (instrument->type == InstrumentType::KIT) {
			// 	int myV = ((x % 4) * 16) + ((y % 4) * 64) + 8;
			// 	int myY = (int)(x / 4) + (int)(y / 4) * 4;

			// 	ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
			// 		((InstrumentClip*)currentSong->currentClip)->getNoteRowOnScreen(myY, modelStackWithTimelineCounter);

			// 	if (modelStackWithNoteRowOnCurrentClip->getNoteRowAllowNull()) {

			// 		instrumentClipView.getRowColour(myY, noteColour);

			// 		noteColour[0] = (char)((noteColour[0] * myV / 255) / 3);
			// 		noteColour[1] = (char)((noteColour[1] * myV / 255) / 3);
			// 		noteColour[2] = (char)((noteColour[2] * myV / 255) / 3);
			// 	}
			// 	else {
			// 		noteColour[0] = 2;
			// 		noteColour[1] = 2;
			// 		noteColour[2] = 2;
			// 	}
			// 	memcpy(image[y][x], noteColour, 3);
			// }
			// Otherwise, square will just get left black, from its having been wiped above

			// Dim note pad if a browser is open with the note highlighted //@TODO: Add API for highlighted notes
			// if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
			// 	|| (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
			// 	if (soundEditor.isUntransposedNoteWithinRange(noteCode)) {
			// 		for (int colour = 0; colour < 3; colour++) {
			// 			int value = (int)image[y][x][colour] + 35;
			// 			image[y][x][colour] = getMin(value, 255);
			// 		}
			// 	}
			// }

			noteCode++;
			yDisplay++;
			noteWithinOctave++;
			if (noteWithinOctave == 12) {
				noteWithinOctave = 0;
			}
		}
	}
	*/
}

void KeyboardLayoutIsomorphic::stopAllNotes() {
	memset(yDisplayActive, 0, sizeof(yDisplayActive));
}

int KeyboardLayoutIsomorphic::noteFromCoords(int x, int y) {

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	if (instrument->type == InstrumentType::KIT) { //

		return 60 + (int)(x / 4) + (int)(y / 4) * 4;
	}
	else {
		InstrumentClip* clip = (InstrumentClip*)currentSong->currentClip;
		return clip->yScrollKeyboardScreen + x + y * clip->keyboardRowInterval;
	}
}

} // namespace keyboard::layout
