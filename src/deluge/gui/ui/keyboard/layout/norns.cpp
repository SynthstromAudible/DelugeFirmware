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

#include "gui/ui/keyboard/layout/norns.h"
#include "definitions.h"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutNorns::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active) {
			currentNotesState.enableNote(noteFromCoords(presses[idxPress].x, presses[idxPress].y),
			                             getDefaultVelocity());
		}
	}
}

void KeyboardLayoutNorns::handleVerticalEncoder(int32_t offset) {
}

void KeyboardLayoutNorns::handleHorizontalEncoder(int32_t offset, bool shiftEnabled) {
}

void KeyboardLayoutNorns::precalculate() {
}

void KeyboardLayoutNorns::renderPads(uint8_t image[][kDisplayWidth + kSideBarWidth][3]) {
	// Iterate over grid image
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t note = noteFromCoords(x, y);
			uint8_t white[3] = {0xFF, 0xFF, 0xFF};

			// If highlighting notes is active, do it
			if (getHighlightedNotes()[note] != 0) {
				colorCopy(image[y][x], white, getHighlightedNotes()[note], 1);
			}
		}
	}
}

} // namespace deluge::gui::ui::keyboard::layout
