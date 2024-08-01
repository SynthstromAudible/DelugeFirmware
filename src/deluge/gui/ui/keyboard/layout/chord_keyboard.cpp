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
#include "hid/display/display.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"
#include "gui/ui/browser/sample_browser.h"

namespace deluge::gui::ui::keyboard::layout {


uint8_t chordTypeSemitoneOffsets[][kMaxChordKeyboardSize] = {
    /* NO_CHORD  */ {0, 0, 0, 0},
    /* FIFTH     */ {7, 0, 0, 0},
    /* SUS2      */ {2, 7, 0, 0},
    /* MINOR     */ {3, 7, 0, 0},
    /* MAJOR     */ {4, 7, 0, 0},
    /* SUS4      */ {5, 7, 0, 0},
    /* MINOR7    */ {3, 7, 10, 0},
    /* DOMINANT7 */ {4, 7, 10, 0},
    /* MAJOR7    */ {4, 7, 11, 0},
};


// void KeyboardLayoutChord::setActiveChord(ChordKeyboardChord chord) {
// 	activeChord = chord;
// 	chordSemitoneOffsets[0] = chordTypeSemitoneOffsets[activeChord][0];
// 	chordSemitoneOffsets[1] = chordTypeSemitoneOffsets[activeChord][1];
// 	chordSemitoneOffsets[2] = chordTypeSemitoneOffsets[activeChord][2];
// 	chordSemitoneOffsets[3] = chordTypeSemitoneOffsets[activeChord][3];
// }

void KeyboardLayoutChord::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes


	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {



			for (int i = 0; i < kMaxChordKeyboardSize; i++) {
				auto offset = chordTypeSemitoneOffsets[pressed.y][i];
				if (!offset) {
					break;
				}
				enableNote(noteFromCoords(pressed.x) + offset, velocity);
			}
		}
	}

	// Should be called last so currentNotesState can be read
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutChord::handleVerticalEncoder(int32_t offset) {
}

void KeyboardLayoutChord::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
	KeyboardStateChord& state = getState().chord;

	state.VoiceOffset += offset;

	precalculate();
}


void KeyboardLayoutChord::precalculate() {
	KeyboardStateChord& state = getState().chord;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kOctaveSize + kDisplayWidth); ++i) {
		noteColours[i] = getNoteColour((i % state.rowInterval) * state.rowColorMultiplier);
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

} // namespace deluge::gui::ui::keyboard::layout
