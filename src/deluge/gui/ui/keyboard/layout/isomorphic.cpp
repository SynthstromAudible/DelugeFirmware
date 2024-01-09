/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/layout/isomorphic.h"
#include "definitions.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutIsomorphic::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		auto pressed = presses[idxPress];
		if (pressed.active && pressed.x < kDisplayWidth) {
			currentNotesState.enableNote(noteFromCoords(pressed.x, pressed.y), velocity);
		}
	}

	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutIsomorphic::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}
	handleHorizontalEncoder(offset * getState().isomorphic.rowInterval, false);
}

void KeyboardLayoutIsomorphic::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
	KeyboardStateIsomorphic& state = getState().isomorphic;

	if (shiftEnabled) {
		state.rowInterval += offset;
		state.rowInterval = std::clamp(state.rowInterval, kMinIsomorphicRowInterval, kMaxIsomorphicRowInterval);

		char buffer[13] = "Row step:   ";
		auto displayOffset = (display->haveOLED() ? 10 : 0);
		intToString(state.rowInterval, buffer + displayOffset, 1);
		display->displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	// Calculate highest possible displayable note with current rowInterval
	int32_t highestScrolledNote =
	    (getHighestClipNote() - ((kDisplayHeight - 1) * state.rowInterval + kDisplayWidth - 1));

	// Make sure current value is in bounds
	state.scrollOffset = std::clamp(state.scrollOffset, getLowestClipNote(), highestScrolledNote);

	// Offset if still in bounds (reject if the next row can not be shown completely)
	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset >= getLowestClipNote() && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutIsomorphic::precalculate() {
	KeyboardStateIsomorphic& state = getState().isomorphic;

	// Pre-Buffer colours for next renderings
	for (int32_t i = 0; i < (kDisplayHeight * state.rowInterval + kDisplayWidth); ++i) {
		getNoteColour(state.scrollOffset + i, noteColours[i]);
	}
}

void KeyboardLayoutIsomorphic::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Precreate list of all active notes per octave
	bool octaveActiveNotes[kOctaveSize] = {0};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		octaveActiveNotes[((currentNotesState.notes[idx].note + kOctaveSize) - getRootNote()) % kOctaveSize] = true;
	}

	// Precreate list of all scale notes per octave
	bool octaveScaleNotes[kModesArraySize] = {0};
	if (getScaleModeEnabled()) {
		ModesArray& scaleNotes = getScaleNotes();
		for (uint8_t idx = 0; idx < getScaleNoteCount(); ++idx) {
			octaveScaleNotes[scaleNotes[idx]] = true;
		}
	}

	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		int32_t noteCode = noteFromCoords(0, y);
		int32_t normalizedPadOffset = noteCode - getState().isomorphic.scrollOffset;
		int32_t noteWithinOctave = (uint16_t)((noteCode + kOctaveSize) - getRootNote()) % kOctaveSize;

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// Full color for every octaves root and active notes
			if (octaveActiveNotes[noteWithinOctave] || noteWithinOctave == 0) {
				memcpy(image[y][x], noteColours[normalizedPadOffset], 3);
			}
			// If highlighting notes is active, do it
			else if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::HighlightIncomingNotes)
			             == RuntimeFeatureStateToggle::On
			         && getHighlightedNotes()[noteCode] != 0) {
				colorCopy(image[y][x], noteColours[normalizedPadOffset], getHighlightedNotes()[noteCode], 1);
			}

			// Or, if this note is just within the current scale, show it dim
			else if (octaveScaleNotes[noteWithinOctave]) {
				getTailColour(image[y][x], noteColours[normalizedPadOffset]);
			}

			//TODO: In a future revision it would be nice to add this to the API
			// Dim note pad if a browser is open with the note highlighted
			if (getCurrentUI() == &sampleBrowser || getCurrentUI() == &audioRecorder
			    || (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem()->isRangeDependent())) {
				if (soundEditor.isUntransposedNoteWithinRange(noteCode)) {
					for (int32_t colour = 0; colour < 3; colour++) {
						int32_t value = (int32_t)image[y][x][colour] + 35;
						image[y][x][colour] = std::min<int32_t>(value, std::numeric_limits<uint8_t>::max());
					}
				}
			}

			++noteCode;
			++normalizedPadOffset;
			noteWithinOctave = (noteWithinOctave + 1) % kOctaveSize;
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
