/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
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

	NoteSet& scaleNotes = getScaleNotes();
	uint8_t scaleNoteCount = getScaleNoteCount();
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (!pressed.active) {
			continue;
		}
		if (pressed.x < kChordKeyboardColumns) {

			int32_t root = getRootNote() + state.noteOffset;
			int32_t octaveDisplacement = (pressed.y + scaleSteps[pressed.x]) / scaleNoteCount;
			int32_t steps = scaleNotes[(pressed.y + scaleSteps[pressed.x]) % scaleNoteCount];

			int32_t note = root + steps + octaveDisplacement * kOctaveSize;
			enableNote(note, velocity);
		}
		else if (pressed.x == kChordKeyboardColumns) {
			for (int32_t i = 0; i < 3; ++i) {
				int32_t root = getRootNote() + state.noteOffset;
				int32_t octaveDisplacement = (pressed.y + scaleSteps[i]) / scaleNoteCount;
				int32_t steps = scaleNotes[(pressed.y + scaleSteps[i]) % scaleNoteCount];

				int32_t note = root + steps + octaveDisplacement * kOctaveSize;
				enableNote(note, velocity);
			}
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
	precalculate(); // Update chord quality colors if scale has changed
}

void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
}

void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                  PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStateChord& state = getState().chord;

	state.noteOffset += offset;

	precalculate();
}

void KeyboardLayoutChord::precalculate() {
	KeyboardStateChord& state = getState().chord;

	Scale currentScale = currentSong->getCurrentScale();
	D_PRINTLN("Current scale: %d", currentScale);

	if (!(acceptedScales.find(currentScale) != acceptedScales.end())) {
		if (display->haveOLED()) {
			display->popupTextTemporary("Chord mode only supports modes of major and minor scales");
		}
		else {
			display->setScrollingText("SCALE NOT SUPPORTED", 0);
		}
	}
	else {
		NoteSet& scaleNotes = getScaleNotes();
		for (int32_t i = 0; i < kDisplayHeight; ++i) {
			// Since each row is an degree of our scale, if we modulate by the inverse of the scale note,
			//  we get the scale modes.
			NoteSet scaleMode = scaleNotes.modulateByOffset(kOctaveSize - scaleNotes[i % scaleNotes.count()]);
			ChordQuality chordQuality = getChordQuality(scaleMode);
			noteColours[i] = qualityColours[chordQuality];
		}

		// for (int32_t i = 0; i < noteColours.size(); ++i) {
		// 	noteColours[i] = colours::cyan;
		// }
	}
}

void KeyboardLayoutChord::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateChord& state = getState().chord;
	// Iterate over grid image
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			if (x < kChordKeyboardColumns) {
				image[y][x] = noteColours[y];
			}
			else if (x == kChordKeyboardColumns) {
				image[y][x] = colours::orange;
			}
			else {
				image[y][x] = colours::black;
			}
		}
	}
}

void KeyboardLayoutChord::drawChordName(int16_t noteCode, const char* chordName, const char* voicingName) {
	char noteName[3] = {0};
	int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
	noteCodeToString(noteCode, noteName, &isNatural, false);

	char fullChordName[300];

	if (voicingName && *voicingName) {
		sprintf(fullChordName, "%s%s - %s", noteName, chordName, voicingName);
	}
	else {
		sprintf(fullChordName, "%s%s", noteName, chordName);
	}

	if (display->haveOLED()) {
		display->popupTextTemporary(fullChordName);
	}
	else {
		int8_t drawDot = !isNatural ? 0 : 255;
		display->setScrollingText(fullChordName, 0);
	}
}

bool KeyboardLayoutChord::allowSidebarType(ColumnControlFunction sidebarType) {
	if (sidebarType == ColumnControlFunction::CHORD) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
