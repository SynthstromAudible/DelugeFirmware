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

#include "io/debug/log.h"
#include "gui/ui/keyboard/layout/chord_keyboard.h"
#include "gui/colour/colour.h"
#include "hid/display/display.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"
#include "gui/ui/browser/sample_browser.h"
#include "util/functions.h"
#include "gui/ui/keyboard/chords.h"


namespace deluge::gui::ui::keyboard::layout {


void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChord& state = getState().chord;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {

			D_PRINTLN("pressed x: %d pressed y: %d", pressed.x, pressed.y);

			int32_t chordNo = pressed.y + state.chordRowOffset;

			// Chord chord = state.chordList.chords[chordNo];
			Chord chord = state.chordList.chords[chordNo];
			// Chord chord = chords2.chords[chordNo];

			bool rootPlayed = false;

			// We need to play the root, but only once
			// enableNote(noteFromCoords(pressed.x), velocity);

			drawChordName(noteFromCoords(pressed.x), chord.name);
			D_PRINTLN("Root x: %d Root y: %d", pressed.x, pressed.y);

			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				// auto offset = chord.voicings[state.voicingOffset[chordNo]].offsets[i];
				Voicing voicing = state.chordList.getVoicing(chordNo);
				int32_t offset = voicing.offsets[i];
				if (!offset && !rootPlayed) {
					rootPlayed = true;
				}
				else if (!offset) {
					continue;
				}
				// D_PRINTLN("Offset: %d, Note: %d", offset, note);
				enableNote(noteFromCoords(pressed.x) + offset, velocity);
			}
		}
	}

	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
	KeyboardStateChord& state = getState().chord;

	if (offset > 0) {
		if (state.chordRowOffset + kDisplayHeight + offset >= kUniqueChords) {
			state.chordRowOffset = kUniqueChords - kDisplayHeight;
		}
		else {
			state.chordRowOffset += offset;
		}
	}
	else {
		if (state.chordRowOffset + offset < 0) {
			state.chordRowOffset = 0;
		}
		else {
			state.chordRowOffset += offset;
		}
	}
	precalculate();

}

void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	KeyboardStateChord& state = getState().chord;

	if (encoderPressed) {
			for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {


				int32_t chordNo = pressed.y + state.chordRowOffset;

				if (offset > 0) {
					if (state.chordList.voicingOffset[chordNo] + offset >= kUniqueVoicings) {
						state.chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
					}
					else {
						state.chordList.voicingOffset[chordNo] += offset;
					}
				}
				else {
					if (state.chordList.voicingOffset[chordNo] + offset < 0) {
						state.chordList.voicingOffset[chordNo] = 0;
					}
					else {
						state.chordList.voicingOffset[chordNo] += offset;
				}

				}
			}

		}


		// 	if (state.voicingOffset[] + kDisplayHeight + offset >= kUniqueChords) {
		// 		state.chordRowOffset = kUniqueChords - kDisplayHeight;
		// 	}
		// 	else {
		// 		state.chordRowOffset += offset;
		// 	}
		// }
		// else {
		// 	if (state.chordRowOffset + offset < 0) {
		// 		state.chordRowOffset = 0;
		// 	}
		// 	else {
		// 		state.chordRowOffset += offset;
		// 	}
		// }
	}
	else {
		state.VoiceOffset += offset;
	}
	precalculate();
}


void KeyboardLayoutChord::precalculate() {
	KeyboardStateChord& state = getState().chord;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kOctaveSize + kDisplayWidth); ++i) {
		noteColours[i] = getNoteColour(((state.VoiceOffset + i) % state.rowInterval) * state.rowColorMultiplier);
	}
}

void KeyboardLayoutChord::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {


	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		// int32_t noteCode = noteFromCoords(0);
		// int32_t normalizedPadOffset = noteCode - getState().chord.scrollOffset;
		// int32_t noteWithinOctave = (uint16_t)((noteCode + kOctaveSize) - getRootNote()) % kOctaveSize;

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// Full colour for every octaves root and active notes
			image[y][x] = noteColours[x];
			// if (octaveActiveNotes[noteWithinOctave] || noteWithinOctave == 0) {
			// }
			// // If highlighting notes is active, do it
			// else if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
			//              == RuntimeFeatureStateToggle::On
			//          && getHighlightedNotes()[noteCode] != 0) {
			// 	image[y][x] = noteColours[normalizedPadOffset].adjust(getHighlightedNotes()[noteCode], 1);
			// }



			// // Or, if this note is just within the current scale, show it dim
			// else if (octaveScaleNotes.has(noteWithinOctave)) {
			// 	image[y][x] = noteColours[normalizedPadOffset].forTail();
			// }

			// // turn off other pads
			// else {
			// 	image[y][x] = colours::black;
			// }

			// TODO: In a future revision it would be nice to add this to the API
			//  Dim note pad if a browser is open with the note highlighted

		}
	}
}


void KeyboardLayoutChord::drawChordName(int16_t noteCode, const char* chordName) {
	// We a modified version of reimplement noteCodeToString here
	// Because sometimes the note name is not displayed correctly
	// and we need to add a null terminator to the note name string
	// TODO: work out how to fix this with the noteCodeToString function
	char noteName[3];
	uint8_t drawDot;
	char* thisChar = noteName;
	D_PRINTLN("Note Name: %s, size of note name: %d", noteName, strlen(noteName));

	int32_t noteCodeWithinOctave = (uint16_t)(noteCode + 120) % (uint8_t)12;
	*thisChar = noteCodeToNoteLetter[noteCodeWithinOctave];
	thisChar++;

	if (noteCodeIsSharp[noteCodeWithinOctave]) {
		*thisChar = display->haveOLED() ? '#' : '.';
		thisChar++;
		drawDot = 255;
	}
	else {
		drawDot = 0;
	}

	*thisChar = '\0';

	// noteName[2] = '\0';

	// noteCodeToString(noteCode, noteName, &isNatural, false);

	D_PRINTLN("Note Name: %s, size of note name: %d", noteName, strlen(noteName));

	if (display->haveOLED()) {
		display->popupTextTemporary(strcat(noteName, chordName));
	}
	else {
		display->setText(strcat(noteName, chordName), false, drawDot, true);
	}
}

} // namespace deluge::gui::ui::keyboard::layout
