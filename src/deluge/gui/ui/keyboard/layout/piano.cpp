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

#include "gui/ui/keyboard/layout/piano.h"
#include "gui/colour/colour.h"
#include "hid/display/display.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

// Handle pads presses
void KeyboardLayoutPiano::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes
	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {
			auto noteInterval = intervalFromCoords(pressed.x, pressed.y);
			if (noteInterval != 0) {
				enableNote(noteFromCoords(pressed.x, pressed.y), velocity);
			}
		}
	}
	// Should be called last so currentNotesState can be read
	ColumnControlsKeyboard::evaluatePads(presses);
}

// Vertical srollOffset (octave+-)
void KeyboardLayoutPiano::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	KeyboardStatePiano& state = getState().piano;
	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset >= lowestPianoOctave && newOffset <= highestPianoOctave) {
		state.scrollOffset += offset;
	}
	precalculate();
}

// Horizontal noteOffset (note+-)
void KeyboardLayoutPiano::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                  PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	KeyboardStatePiano& state = getState().piano;
	int32_t newNoteOffset = state.noteOffset + offset;
	// allow to shift horizontally only for 8 semi (1 oct), for more - use a vertical scroll
	if (newNoteOffset >= 0 && newNoteOffset <= highestNoteOffset) {
		state.noteOffset += offset;
	}
}

// Fill up noteColours array with colours
void KeyboardLayoutPiano::precalculate() {
	KeyboardStatePiano& state = getState().piano;
	for (int32_t i = 0; i <= totalPianoOctaves; ++i) {
		noteColours[i] = getNoteColour((state.scrollOffset + i) * colourOffset);
	}
}

// Render RGB pads
void KeyboardLayoutPiano::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Precreate list of all active notes per octave
	bool octaveActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		octaveActiveNotes[((currentNotesState.notes[idx].note + kOctaveSize) - getRootNote()) % kOctaveSize] = true;
	}
	// Precreate list of all scale notes per octave
	NoteSet octaveScaleNotes;
	if (getScaleModeEnabled()) {
		NoteSet& scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes.add(scaleNotes[idx]);
		}
	}
	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			auto noteInterval = intervalFromCoords(x, y);
			if (noteInterval != 0) {
				auto note = noteFromCoords(x, y);
				int32_t noteWithinOctave = (uint16_t)((note + kOctaveSize) - getRootNote()) % kOctaveSize;
				RGB colourSource = noteColours[y / 2];
				// Active Root Note: Full brightness and colour
				if (noteWithinOctave == 0 && octaveActiveNotes[noteWithinOctave]) {
					image[y][x] = colourSource.adjust(255, 1);
				}
				// Highlight incomming notes
				else if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
				             == RuntimeFeatureStateToggle::On
				         && getHighlightedNotes()[note] != 0) {
					image[y][x] = colourSource.adjust(getHighlightedNotes()[note], 1);
				}
				// Inactive Root Note: Full colour but less brightness
				else if (noteWithinOctave == 0) {
					image[y][x] = colourSource.adjust(255, 2);
				}
				// Active Non-Root note in other octaves: Toned down colour but high brightness
				else if (octaveActiveNotes[noteWithinOctave]) {
					image[y][x] = colourSource.adjust(127, 1);
				}
				// Other notes in a scale (or all notes if no scale): Toned down a little and low brighness
				else if (octaveScaleNotes.has(noteWithinOctave) || !getScaleModeEnabled()) {
					image[y][x] = colourSource.adjust(186, 3);
				}
				// Non-scale notes: Dark tone, low brightness
				else {
					image[y][x] = colourSource.adjust(64, 3);
				}
				// No note at all on this pad
			}
			else {
				image[y][x] = colours::black;
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
