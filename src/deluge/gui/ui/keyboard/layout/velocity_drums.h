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

#pragma once

#include "gui/ui/keyboard/layout.h"

namespace deluge::gui::ui::keyboard::layout {

constexpr int32_t kMinZoomLevel = 0;
constexpr int32_t kMaxZoomLevel = 12;

// The zoomArr is used to set the edge sizes of the pads {x size, y size} on each zoom level.
const int32_t zoomArr[13][2] = {{1, 1}, {2, 1}, {3, 1}, {2, 2}, {3, 2}, {4, 2}, {5, 2},
                                {3, 4}, {4, 4}, {5, 4}, {8, 4}, {8, 8}, {16, 8}};

class KeyboardLayoutVelocityDrums : KeyboardLayout {
public:
	KeyboardLayoutVelocityDrums() {}
	~KeyboardLayoutVelocityDrums() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	char const* name() override { return "Drums"; }
	bool supportsInstrument() override { return false; }
	bool supportsKit() override { return true; }

private:
	RGB noteColours[128];

	// inline uint8_t noteFromCoords(int32_t x, int32_t y, uint8_t edgeSizeX, uint8_t edgeSizeY) {
	// 	uint8_t padsPerRow = kDisplayWidth / edgeSizeX;
	// 	uint8_t x_adjust = (x == 15 && edgeSizeX % 2 == 1 && edgeSizeX > 1) ? 1 : 0;
	// 	return (x / edgeSizeX) - x_adjust + ((y / edgeSizeY) * padsPerRow) + getState().drums.scrollOffset;
	// }

	inline uint8_t velocityFromCoords(int32_t x, int32_t y, uint8_t edgeSizeX, uint8_t edgeSizeY) {
		uint8_t velocity = 0;
		if (edgeSizeX == 1) {
			// No need to do a lot of calculations or use max velocity for only one option.
			velocity = FlashStorage::defaultVelocity * 2;
		}
		else{
			bool oddPad = (edgeSizeX % 2 == 1);  // check if has odd width pads
			uint8_t x_limit = kDisplayWidth - 2 - edgeSizeX; // end of second to last pad in a row (the regular pads)
			bool x_adjust = (oddPad && x > x_limit);
			uint8_t localX = x_adjust ? x - x_limit : x % (edgeSizeX);

			if (edgeSizeY == 1) {
				velocity = (localX + 1) * 200 / (edgeSizeX + x_adjust); // simpler, easier on the ears.
			}
			else {
				if(edgeSizeX % 2 == 1 && x > kDisplayWidth - 2 - edgeSizeX) edgeSizeX += 1;
				uint8_t position = localX + 1;
				position += ((y % edgeSizeY) * (edgeSizeX + x_adjust));
				// We use two bytes to keep the precision of the calculations high,
				// then shift it down to one byte at the end.
				uint32_t stepSize = 0xFFFF / ((edgeSizeX + x_adjust) * edgeSizeY);
				velocity = (position * stepSize) >> 8;
			}
		}
		return velocity; // returns an integer value 0-255
	}
};

}; // namespace deluge::gui::ui::keyboard::layout
