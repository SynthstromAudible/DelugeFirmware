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
#include "gui/ui/keyboard/keyboard_screen.h" //@TODO: Try to factor out

namespace keyboard::layout {

void KeyboardLayoutIsomorphic::handlePad(int x, int y, int velocity) {

	int noteCode = noteFromCoords(x, y);



	// Press-down
	if (velocity) {
		if(notesFull()) {
			return;
		}

		int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == INSTRUMENT_TYPE_KIT) { //
			yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
		}
		if (yDisplayActive[yDisplay]) {
			return;
		}

		// Change editing range if necessary
		if (instrument->type == INSTRUMENT_TYPE_SYNTH) {
			if (velocity) {
				if (getCurrentUI() == &soundEditor
					&& soundEditor.getCurrentMenuItem() == &menu_item::multiRangeMenu) {
					menu_item::multiRangeMenu.noteOnToChangeRange(noteCode
																	+ ((SoundInstrument*)instrument)->transpose);
				}
			}
		}

		// Ensure the note the user is trying to sound isn't already sounding
		NoteRow* noteRow = ((InstrumentClip*)instrument->activeClip)->getNoteRowForYNote(noteCode);
		if (noteRow) {
			if (noteRow->soundingStatus == STATUS_SEQUENCED_NOTE) {
				return;
			}
		}

		// Only now that we know we're not going to return prematurely can we mark the pad as pressed
		{
			padPresses[emptyPressIndex].x = x;
			padPresses[emptyPressIndex].y = y;
			yDisplayActive[yDisplay] = true;
		}

		{
			if (instrument->type == INSTRUMENT_TYPE_KIT) {
				int velocityToSound = ((x % 4) * 8) + ((y % 4) * 32) + 7;
				instrumentClipView.auditionPadAction(velocityToSound, yDisplay, false);
			}
			else {
				int velocityToSound = instrument->defaultVelocity;
				((MelodicInstrument*)instrument)
					->beginAuditioningForNote(modelStack, noteCode, velocityToSound, zeroMPEValues);
			}
		}

		drawNoteCode(noteCode);
		enterUIMode(UI_MODE_AUDITIONING);

		// Begin resampling - yup this is even allowed if we're in the card routine!
		if (Buttons::isButtonPressed(hid::button::RECORD) && !audioRecorder.recordingSource) {
			audioRecorder.beginOutputRecording();
			Buttons::recordButtonPressUsedUp = true;
		}
	}

	// Press-up
	else {

		int p;
		for (p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
			if (padPresses[p].x == x && padPresses[p].y == y) {
				goto foundIt;
			}
		}

		// There were no presses. Just check we're not still stuck in "auditioning" mode, as users have still been reporting problems with this. (That comment from around 2021?)
		if (isUIModeActive(UI_MODE_AUDITIONING)) {
			exitUIMode(UI_MODE_AUDITIONING);
		}
		return;

foundIt:
		padPresses[p].x = 255;
		noteCode = noteFromCoords(x, y);
		int yDisplay = noteCode - getCurrentClip()->yScrollKeyboardScreen;
		Instrument* instrument = (Instrument*)currentSong->currentClip->output;
		if (instrument->type == INSTRUMENT_TYPE_KIT) { //
			yDisplay = (int)(x / 4) + (int)(y / 4) * 4;
		}

		// We need to check that we had actually switched the note on here - it might have already been sounding, from the sequence
		if (!yDisplayActive[yDisplay]) {
			return;
		}

		// If any other of the same note is being held down, then don't switch it off. Also, see if we're still "auditioning" any notes at all
		exitUIMode(UI_MODE_AUDITIONING);

		// If any pad press still happening...
		for (p = 0; p < MAX_NUM_KEYBOARD_PAD_PRESSES; p++) {
			if (padPresses[p].x != 255) {
				enterUIMode(UI_MODE_AUDITIONING);
				// ...then we're still auditioning

				// If the same note is still being held down (on a different pad), then we don't want to switch it off either
				if (noteFromCoords(padPresses[p].x, padPresses[p].y) == noteCode) {
					return;
				}
			}
		}

		// If we had indeed sounded the note via audition (as opposed to it being on in the sequence), switch it off.
		// If that was not the case, well, we still did want to potentially exit audition mode above, cos users been reporting stuck note problems,
		// even though I can't see quite how we'd get stuck there
		if (yDisplayActive[yDisplay]) {

			if (instrument->type == INSTRUMENT_TYPE_KIT) { //
				instrumentClipView.auditionPadAction(0, yDisplay, false);
			}
			else {

				((MelodicInstrument*)instrument)->endAuditioningForNote(modelStack, noteCode);
			}

			yDisplayActive[yDisplay] = false;
		}

		// If anything at all still auditioning...
		int highestNoteCode = getHighestAuditionedNote();
		if (highestNoteCode != -2147483648) {
			drawNoteCode(highestNoteCode);
		}
		else {
#if HAVE_OLED
			OLED::removePopup();
#else
			redrawNumericDisplay();
#endif
		}
	}


}

void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][displayWidth + sideBarWidth][3]) { //@TODO: Refactor
// First, piece together a picture of all notes-within-an-octave which are active
bool notesWithinOctaveActive[12];
memset(notesWithinOctaveActive, 0, sizeof(notesWithinOctaveActive));
for(const auto& note : activeNotes) {
	int noteWithinOctave = (note - getRootNote() + 120) % 12;
	notesWithinOctaveActive[noteWithinOctave] = true;
}

uint8_t noteColour[] = {127, 127, 127};

for (int y = 0; y < displayHeight; y++) {
	int noteCode = noteFromCoords(0, y);
	int yDisplay = noteCode - ((InstrumentClip*)currentSong->currentClip)->yScrollKeyboardScreen;
	int noteWithinOctave = (uint16_t)(noteCode - currentSong->rootNote + 120) % (uint8_t)12;

	for (int x = 0; x < displayWidth; x++) {

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
		// if (instrument->type == INSTRUMENT_TYPE_KIT) {
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

int KeyboardLayoutIsomorphic::noteFromCoords(int x, int y) {

	Instrument* instrument = (Instrument*)currentSong->currentClip->output;
	if (instrument->type == INSTRUMENT_TYPE_KIT) { //

		return 60 + (int)(x / 4) + (int)(y / 4) * 4;
	}
	else {
		InstrumentClip* clip = (InstrumentClip*)currentSong->currentClip;
		return clip->yScrollKeyboardScreen + x + y * clip->keyboardRowInterval;
	}
}


};
