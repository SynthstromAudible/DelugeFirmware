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

namespace keyboard::layout {};

// for drum kit velocity calculation is:
// int velocityToSound = ((x % 4) * 8) + ((y % 4) * 32) + 7; //@TODO: Get velocity from note

//void KeyboardLayoutIsomorphic::handleVerticalEncoder(int offset) {
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

///void KeyboardLayoutIsomorphic::handleHorizontalEncoder(int offset, bool shiftEnabled) {
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

// void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) { //@TODO: Refactor

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
