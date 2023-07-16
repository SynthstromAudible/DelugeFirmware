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

namespace keyboard::layout {

void KeyboardLayoutIsomorphic::handlePad(int x, int y, int velocity) {

}


void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][displayWidth + sideBarWidth][3]) {
	/*
	// First, piece together a picture of all notes-within-an-octave which are active
	bool notesWithinOctaveActive[12];
	memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
	for (int p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
		if (padPresses[p].x != 255) {
			int noteCode = getNoteCodeFromCoords(padPresses[p].x, padPresses[p].y);
			int noteWithinOctave = (noteCode - currentSong->rootNote + 120) % 12;
			notesWithinOctaveActive[noteWithinOctave] = true;
		}
	}



	uint8_t noteColour[] = {127, 127, 127};
	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(currentSong->currentClip);

	// Flashing default root note
	if (uiTimerManager.isTimerSet(TIMER_DEFAULT_ROOT_NOTE)) {
		if (flashDefaultRootNoteOn) {
			for (int y = 0; y < displayHeight; y++) {
				int noteCode = getNoteCodeFromCoords(0, y);
				int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
				int noteWithinOctave = (noteCode - defaultRootNote + 120) % 12;
				for (int x = 0; x < displayWidth; x++) {

					if (!noteWithinOctave) {
						memcpy(image[y][x], noteColours[yDisplay], 3);
					}

					yDisplay++;
					noteWithinOctave++;
					if (noteWithinOctave == 12) {
						noteWithinOctave = 0;
					}
				}
			}
		}
	}

	// Or normal
	else {
		for (int y = 0; y < displayHeight; y++) {
			int noteCode = getNoteCodeFromCoords(0, y);
			int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
			int noteWithinOctave = (uint16_t)(noteCode - currentSong->rootNote + 120) % (uint8_t)12;

			for (int x = 0; x < displayWidth; x++) {

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
					if (getCurrentClip()->inScaleMode && currentSong->modeContainsYNote(noteCode)) {
						getTailColour(image[y][x], noteColours[yDisplay]);
					}
				}

				if (instrument->type == INSTRUMENT_TYPE_KIT) {
					int myV = ((x % 4) * 16) + ((y % 4) * 64) + 8;
					int myY = (int)(x / 4) + (int)(y / 4) * 4;

					ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip =
					    getCurrentClip()->getNoteRowOnScreen(myY, modelStackWithTimelineCounter);

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
				}
				// Otherwise, square will just get left black, from its having been wiped above

				// If we're selecting ranges...
				if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
				    || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
					if (soundEditor.isUntransposedNoteWithinRange(noteCode)) {
						for (int colour = 0; colour < 3; colour++) {
							int value = (int)image[y][x][colour] + 35;
							image[y][x][colour] = getMin(value, 255);
						}
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
	*/
}

};
