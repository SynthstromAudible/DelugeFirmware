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

void KeyboardLayoutPiano::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {
			auto note = noteFromCoords(pressed.x, pressed.y);
			if (note != 255) {
				enableNote(note, velocity);
			}
		}
	}

	// Should be called last so currentNotesState can be read
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutPiano::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	offsetPads(offset * getState().piano.rowInterval, false);
}

void KeyboardLayoutPiano::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                  PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {
	offset *= 7;
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	if (encoderPressed) {
		if (offset > 0) {
			if (zoom == 1) {
				zoom = 2;
			}
			else if (zoom == 2) {
				zoom = 4;
			}
		}
		else if (offset < 0) {
			if (zoom == 4) {
				zoom = 2;
			}
			else if (zoom == 2) {
				zoom = 1;
			}
		}
		//precalculate();
	}
	else {
		offsetPads(offset, shiftEnabled);
	}
}

void KeyboardLayoutPiano::offsetPads(int32_t offset, bool shiftEnabled) {
	KeyboardStatePiano& state = getState().piano;

	if (shiftEnabled) {
		state.rowInterval += offset;
		state.rowInterval = std::clamp(state.rowInterval, kMinPianoRowInterval, kMaxPianoRowInterval);

		char buffer[13] = "Row step:   ";
		int32_t displayOffset = (display->haveOLED() ? 10 : 0);
		intToString(state.rowInterval, buffer + displayOffset, 1);
		display->displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	// Calculate highest and lowest possible displayable note with current rowInterval
	int32_t lowestScrolledNote = padIndexFromNote(getLowestClipNote());
	int32_t highestScrolledNote =
	    (padIndexFromNote(getHighestClipNote()) - ((kDisplayHeight - 1) * state.rowInterval + kDisplayWidth - 1));

	// Make sure current value is in bounds
	state.scrollOffset = std::clamp(state.scrollOffset, lowestScrolledNote, highestScrolledNote);

	// Offset if still in bounds (reject if the next row can not be shown completely)
	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset >= lowestScrolledNote && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutPiano::precalculate() {
	KeyboardStatePiano& state = getState().piano;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kDisplayHeight * state.rowInterval + kDisplayWidth); ++i) {
		noteColours[i] = getNoteColour(noteFromPadIndex(state.scrollOffset + i));
	}
}

void KeyboardLayoutPiano::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Precreate list of all active notes per octave
	bool scaleActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		scaleActiveNotes[((currentNotesState.notes[idx].note + kOctaveSize) - getRootNote()) % kOctaveSize] = true;
	}

	uint8_t scaleNoteCount = getScaleNoteCount();

	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			auto note = noteFromCoords(x, y);

			int32_t yZoom = y / zoom;

			if (keyLayout[yZoom][x] == KeyType::BLACK) {
				if (currentNotesState.noteEnabled(note)) {
					image[y][x] = RGB::monochrome(10);
				}
				else {
					image[y][x] = colours::grey;
				}
			}
			else if (keyLayout[yZoom][x] == KeyType::WHITE) {
				if (currentNotesState.noteEnabled(note)) {
					image[y][x] = colours::white_full;
				}
				else {
					image[y][x] = colours::white;
				}
			}
			else {
				image[y][x] = colours::black;
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
