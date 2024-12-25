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

constexpr int32_t kMinZoomLevel = 1;
constexpr int32_t kMaxZoomLevel = 12;

// The zoomArr is used to set the edge sizes of the pads {x size, y size} on each zoom level.
constexpr int32_t zoomArr[12][2] = {{1, 1}, {2, 1}, {3, 1}, {2, 2}, {3, 2}, {4, 2},
                                    {5, 2}, {3, 4}, {4, 4}, {5, 4}, {8, 4}, {8, 8}};

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
	inline uint8_t noteFromCoords(int32_t x, int32_t y, uint8_t edgeSizeX, uint8_t edgeSizeY) {
		uint8_t padsPerRow = kDisplayWidth / edgeSizeX;
		return (x / edgeSizeX) + ((y / edgeSizeY) * padsPerRow) + getState().drums.scrollOffset;
	}

	inline uint8_t velocityFromCoords(int32_t x, int32_t y, uint8_t edgeSizeX, uint8_t edgeSizeY) {

		if (edgeSizeX == 1) {
			// No need to do a lot of calculations or use max velocity for only one option.
			return FlashStorage::defaultVelocity;
		}
		else {
			uint8_t position = (x % edgeSizeX) + 1;
			if (edgeSizeY > 1) { // only need to calculate y position if we have more than one row per pad
				position += ((y % edgeSizeY) * edgeSizeX);
			}
			// We use two bytes to keep the precision of the calculations high,
			// then shift it down to one byte at the end
			uint32_t stepSize = 0xFFFF / (edgeSizeX * edgeSizeY);
			return (position * stepSize) >> 8;
		}
	}

	RGB noteColours[kDisplayHeight * kDisplayWidth];
};

}; // namespace deluge::gui::ui::keyboard::layout
