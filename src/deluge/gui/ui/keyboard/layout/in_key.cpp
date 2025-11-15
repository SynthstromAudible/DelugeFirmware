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
#include "gui/colour/colour.h"
#include "hid/display/display.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {
			enableNote(noteFromCoords(pressed.x, pressed.y), velocity);
		}
	}

	// Should be called last so currentNotesState can be read
	ColumnControlsKeyboard::evaluatePads(presses);
}

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	offsetPads(offset * getState().inKey.rowInterval, false);
}

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                   PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                   bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}
	offsetPads(offset, shiftEnabled);
}

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::offsetPads(int32_t offset, bool shiftEnabled) {
	KeyboardStateInKey& state = getState().inKey;

	if (shiftEnabled) {
		state.rowInterval += offset;
		state.rowInterval = std::clamp(state.rowInterval, kMinInKeyRowInterval, kMaxInKeyRowInterval);

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

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::precalculate() {
	KeyboardStateInKey& state = getState().inKey;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kDisplayHeight * state.rowInterval + kDisplayWidth); ++i) {
		noteColours[i] = getNoteColour(noteFromPadIndex(state.scrollOffset + i));
	}
}

PLACE_SDRAM_TEXT void KeyboardLayoutInKey::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
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
			RGB colourSource = noteColours[padIndex - getState().inKey.scrollOffset];

			// Full brightness and colour for active root note
			if (noteWithinScale == 0 && scaleActiveNotes[noteWithinScale]) {
				image[y][x] = colourSource.adjust(255, 1);
			}
			// If highlighting notes is active, do it
			else if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
			             == RuntimeFeatureStateToggle::On
			         && getHighlightedNotes()[note] != 0) {
				image[y][x] = colourSource.adjust(getHighlightedNotes()[note], 1);
			}
			// Full colour but less brightness for inactive root note
			else if (noteWithinScale == 0) {
				image[y][x] = colourSource.adjust(255, 2);
			}
			// Toned down colour but high brightness for active scale note
			else if (scaleActiveNotes[noteWithinScale]) {
				image[y][x] = colourSource.adjust(127, 3);
			}
			// Dimly white for inactive scale notes
			else {
				image[y][x] = RGB::monochrome(1);
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
