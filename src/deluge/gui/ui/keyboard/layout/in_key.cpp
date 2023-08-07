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

#include "gui/ui/keyboard/layout/in_key.h"
#include "definitions.h"
#include "gui/colour.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutInKey::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutInKey::handleVerticalEncoder(int32_t offset) {
	handleHorizontalEncoder(offset * getState().inKey.rowInterval, false);
}

void KeyboardLayoutInKey::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
	KeyboardStateInKey& state = getState().inKey;

	if (shiftEnabled) {
		state.rowInterval += offset;
		state.rowInterval = std::max(state.rowInterval, kMinInKeyRowInterval);
		state.rowInterval = std::min(kMaxInKeyRowInterval, state.rowInterval);

		char buffer[13] = "Row step:   ";
		intToString(state.rowInterval, buffer + (HAVE_OLED ? 10 : 0), 1);
		numericDriver.displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	// Calculate highest and lowest possible displayable note with current rowInterval
	int32_t lowestScrolledNote = padIndexFromNote(getLowestClipNote());
	int32_t highestScrolledNote =
	    (padIndexFromNote(getHighestClipNote()) - ((kDisplayHeight - 1) * state.rowInterval + kDisplayWidth - 1));

	// Make sure current value is in bounds
	state.scrollOffset = std::max(lowestScrolledNote, state.scrollOffset);
	state.scrollOffset = std::min(state.scrollOffset, highestScrolledNote);

	// Offset if still in bounds (reject if the next row can not be shown completely)
	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset >= lowestScrolledNote && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutInKey::precalculate() {
	KeyboardStateInKey& state = getState().inKey;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kDisplayHeight * state.rowInterval + kDisplayWidth); ++i) {
		noteColours[i] = getNoteColour(noteFromPadIndex(state.scrollOffset + i));
	}
}

inline Colour prepareColour(const Colour& colour, uint8_t intensity, uint8_t brightnessDivider) {
	return colour.transform([intensity, brightnessDivider](uint8_t channel) { //<
		return ((channel * intensity / 255) / brightnessDivider);
	});
}

void KeyboardLayoutInKey::renderPads(Colour image[][kDisplayWidth + kSideBarWidth]) {
	// Precreate list of all active notes per octave
	bool scaleActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		scaleActiveNotes[((currentNotesState.notes[idx].note + kOctaveSize) - getRootNote()) % kOctaveSize] = true;
	}

	uint8_t scaleNoteCount = getScaleNoteCount();

	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			auto padIndex = padIndexFromCoords(x, y);
			auto note = noteFromPadIndex(padIndex);
			int32_t noteWithinScale = (uint16_t)((note + kOctaveSize) - getRootNote()) % kOctaveSize;
			Colour colourSource = noteColours[padIndex - getState().inKey.scrollOffset];

			// Full brightness and colour for active root note
			if (noteWithinScale == 0 && scaleActiveNotes[noteWithinScale]) {
				image[y][x] = prepareColour(colourSource, 255, 1);
			}
			// Full colour but less brightness for inactive root note
			else if (noteWithinScale == 0) {
				image[y][x] = prepareColour(colourSource, 255, 2);
			}
			// TOned down colour but high brightness for active scale note
			else if (scaleActiveNotes[noteWithinScale]) {
				image[y][x] = prepareColour(colourSource, 127, 3);
			}
			// Dimly white for inactive scale notes
			else {
				image[y][x] = Colour::monochrome(1);
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
