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

#include "gui/ui/keyboard/layout/chord_library.h"
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

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	// We run through the presses in reverse order to display the most recent pressed chord on top
	for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

		PressedPad pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {

			int32_t chordNo = getChordNo(pressed.y);

			Voicing voicing = state.chordList.getChordVoicing(chordNo);
			drawChordName(noteFromCoords(pressed.x), state.chordList.chords[chordNo].name, voicing.supplementalName);

			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				int32_t offset = voicing.offsets[i];
				if (offset == NONE) {
					continue;
				}
				enableNote(noteFromCoords(pressed.x) + offset, velocity);
			}
		}
	}
	ColumnControlsKeyboard::evaluatePads(presses);
}

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	state.chordList.adjustChordRowOffset(offset);
	precalculate();
}

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                          PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                          bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStateChordLibrary& state = getState().chordLibrary;

	if (encoderPressed) {
		for (int32_t idxPress = kMaxNumKeyboardPadPresses - 1; idxPress >= 0; --idxPress) {

			PressedPad pressed = presses[idxPress];
			if (pressed.active && pressed.x < kDisplayWidth) {

				int32_t chordNo = getChordNo(pressed.y);

				state.chordList.adjustVoicingOffset(chordNo, offset);
			}
		}
	}
	else {
		state.noteOffset += offset;
	}
	precalculate();
}

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::precalculate() {
	KeyboardStateChordLibrary& state = getState().chordLibrary;
	// On first render, offset by the root note. This can't be done in the constructor
	// because at constructor time, root note changes from the default menu aren't seen yet
	// or if the root note is changed in the song also isn't seen.
	if (!initializedNoteOffset) {
		initializedNoteOffset = true;
		state.noteOffset += getRootNote();
	}

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < noteColours.size(); ++i) {
		noteColours[i] = getNoteColour(((state.noteOffset + i) % state.rowInterval) * state.rowColorMultiplier);
	}
	uint8_t hueStepSize = 192 / (kVerticalPages - 1); // 192 is the hue range for the rainbow
	for (int32_t i = 0; i < pageColours.size(); ++i) {
		pageColours[i] = getNoteColour(i * hueStepSize);
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	KeyboardStateChordLibrary& state = getState().chordLibrary;
	bool inScaleMode = getScaleModeEnabled();

	// Precreate list of all scale notes per octave
	NoteSet octaveScaleNotes;
	if (inScaleMode) {
		NoteSet& scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes.add(scaleNotes[idx]);
		}
	}

	// Iterate over grid image
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		int32_t noteCode = noteFromCoords(x);
		uint16_t noteWithinOctave = (uint16_t)((noteCode + kOctaveSize) - getRootNote()) % kOctaveSize;

		for (int32_t y = 0; y < kDisplayHeight; ++y) {
			int32_t chordNo = getChordNo(y);
			int32_t pageNo = chordNo / kDisplayHeight;

			if (inScaleMode) {
				NoteSet intervalSet = state.chordList.chords[chordNo].intervalSet;
				NoteSet modulatedNoteSet = intervalSet.modulateByOffset(noteWithinOctave);

				if (modulatedNoteSet.isSubsetOf(octaveScaleNotes)) {
					image[y][x] = noteColours[x % noteColours.size()];
				}
				else {
					image[y][x] = noteColours[x % noteColours.size()].dim(4);
				}
			}
			else {
				// show we've the last column, show the page colour for navigation
				if ((x == kDisplayWidth - 1) || (x == kDisplayWidth - 2)) {
					image[y][x] = pageColours[pageNo % pageColours.size()].dim(1);
				}
				// If not in scale mode, highlight the root note
				else if (noteWithinOctave == 0) {
					image[y][x] = noteColours[x % noteColours.size()];
				}
				else {
					image[y][x] = noteColours[x % noteColours.size()].dim(4);
				}
			}
		}
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutChordLibrary::drawChordName(int16_t noteCode, const char* chordName,
                                                                const char* voicingName) {
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

PLACE_SDRAM_TEXT bool KeyboardLayoutChordLibrary::allowSidebarType(ColumnControlFunction sidebarType) {
	if ((sidebarType == ColumnControlFunction::CHORD)) {
		return false;
	}
	return true;
}

} // namespace deluge::gui::ui::keyboard::layout
