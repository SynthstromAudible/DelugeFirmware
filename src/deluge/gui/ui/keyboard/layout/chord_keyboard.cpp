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

#include "gui/ui/keyboard/layout/chord_keyboard.h"
#include "gui/colour/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"
#include <stdlib.h>

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChord& state = getState().chord;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {

			int32_t chordNo = pressed.y + state.chordRowOffset;
			Chord chord = state.chordList.chords[chordNo];
			bool rootPlayed = false;

			drawChordName(noteFromCoords(pressed.x), chord.name);

			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				Voicing voicing = state.chordList.getVoicing(chordNo);
				int32_t offset = voicing.offsets[i];
				if (!offset && !rootPlayed) {
					rootPlayed = true;
				}
				else if (!offset) {
					continue;
				}
				enableNote(noteFromCoords(pressed.x) + offset, velocity);
			}
		}
	}

	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
	KeyboardStateChord& state = getState().chord;

	if (offset > 0) {
		state.chordRowOffset = std::min<int32_t>(kUniqueChords - kDisplayHeight, state.chordRowOffset + offset);
	}
	else {
		state.chordRowOffset = std::max<int32_t>(0, state.chordRowOffset + offset);
	}
	precalculate();
}

void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                  PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	KeyboardStateChord& state = getState().chord;

	if (encoderPressed) {
		for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {

				int32_t chordNo = pressed.y + state.chordRowOffset;

				if (offset > 0) {
					state.chordList.voicingOffset[chordNo] =
					    std::min<int32_t>(kUniqueVoicings - 1, state.chordList.voicingOffset[chordNo] + offset);
				}
				else {
					state.chordList.voicingOffset[chordNo] =
					    std::max<int32_t>(0, state.chordList.voicingOffset[chordNo] + offset);
				}
			}
		}
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
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[y][x] = noteColours[x];
		}
	}
}

void KeyboardLayoutChord::drawChordName(int16_t noteCode, const char* chordName) {
	char noteName[3];
	uint8_t drawDot;
	char* thisChar = noteName;

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

	if (display->haveOLED()) {
		display->popupTextTemporary(strcat(noteName, chordName));
	}
	else {
		display->setText(strcat(noteName, chordName), false, drawDot, true);
	}
}
} // namespace deluge::gui::ui::keyboard::layout
