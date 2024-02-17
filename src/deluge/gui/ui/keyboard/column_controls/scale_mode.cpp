
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

#include "scale_mode.h"
#include "gui/ui/keyboard/layout/column_controls.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

void ScaleModeColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t otherChannels = 0;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		bool mode_selected = y == currentScalePad;
		uint8_t mode_available = y < NUM_PRESET_SCALES ? 0x7f : 0;
		otherChannels = mode_selected ? 0xf0 : 0;
		uint8_t base = mode_selected ? 0xff : mode_available;
		image[y][column] = {base, base, otherChannels};
	}
}

bool ScaleModeColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	int32_t newScale = ((int32_t)scaleModes[pad] + offset) % NUM_PRESET_SCALES;
	if (newScale < 0) {
		newScale = NUM_PRESET_SCALES - 1;
	}
	scaleModes[pad] = newScale;
	return true;
};

void ScaleModeColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                                KeyboardLayout* layout) {
	if (pad.active) {
		if (keyboardScreen.setScale(scaleModes[pad.y])) {
			currentScalePad = pad.y;
		}
	}
	else if (!pad.padPressHeld) {
		// Pad released after short press
		if (keyboardScreen.setScale(scaleModes[pad.y])) {
			previousScalePad = pad.y;
			currentScalePad = pad.y;
		}
	}
	else {
		// Pad released after long press
		if (keyboardScreen.setScale(scaleModes[previousScalePad])) {
			currentScalePad = previousScalePad;
		}
	}
};
} // namespace deluge::gui::ui::keyboard::controls
