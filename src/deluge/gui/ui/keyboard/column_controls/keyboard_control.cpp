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

#include "keyboard_control.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "gui/ui/keyboard/layout/chord_keyboard.h"


using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

void KeyboardControlColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) {
	if (layout->name() == "Chord") {
		auto* chordLayout = static_cast<KeyboardLayoutChord*>(layout);
		KeyboardStateChord& state = getCurrentInstrumentClip()->keyboardState.chord;
		// image[y][column] = colours::black;
		for (int8_t y = 0; y < kDisplayHeight; y++) {
			if (y == 0) {
				if (state.autoVoiceLeading) {
					image[y][column] = colours::green;
				}
				else {
					image[y][column] = colours::red;
				}
			}
			else if (y == kDisplayHeight - 1) {
				image[y][column] =
					chordLayout->mode == ChordKeyboardMode::ROW ? colours::blue : colours::blue.forTail(); // Row mode
			}
			else if (y == kDisplayHeight - 2) {
				image[y][column] =
					chordLayout->mode == ChordKeyboardMode::COLUMN ? colours::purple : colours::purple.forTail(); // Column mode
			}
			else {
				image[y][column] = colours::black;
			}
		}
	}
}

bool KeyboardControlColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void KeyboardControlColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                      KeyboardLayout* layout) {
};

void KeyboardControlColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                            KeyboardLayout* layout) {

	if (layout->name() == "Chord") {
		if (pad.active) {
			auto* chordLayout = static_cast<KeyboardLayoutChord*>(layout);
			KeyboardStateChord& state = getCurrentInstrumentClip()->keyboardState.chord;
			if (pad.y == 0) {
				state.autoVoiceLeading = !state.autoVoiceLeading;
				if (state.autoVoiceLeading) {
					char const* shortLong[2] = {"AUTO", "Auto Voice Leading: Beta"};
					display->displayPopup(shortLong);
				}
			}
			else if (pad.y == kDisplayHeight - 1) {
				chordLayout->mode = ChordKeyboardMode::ROW;
				char const* shortLong[2] = {"ROW", "Chord Row Mode"};
				display->displayPopup(shortLong);
			}
			else if (pad.y == kDisplayHeight - 2) {
				chordLayout->mode = ChordKeyboardMode::COLUMN;
				char const* shortLong[2] = {"COLM", "Chord Column Mode"};
				display->displayPopup(shortLong);
			}
		}
	}
};
} // namespace deluge::gui::ui::keyboard::controls
