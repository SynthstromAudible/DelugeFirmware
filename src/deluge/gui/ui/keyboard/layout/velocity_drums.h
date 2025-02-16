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

constexpr int32_t k_min_zoom_level = 0;
constexpr int32_t k_max_zoom_level = 12;

// The zoom_arr is used to set the edge sizes of the pads {x size, y size} on each zoom level.
const int32_t zoom_arr[13][2] = {{1, 1}, {2, 1}, {3, 1}, {2, 2}, {3, 2}, {4, 2}, {5, 2},
                                 {3, 4}, {4, 4}, {5, 4}, {8, 4}, {8, 8}, {16, 8}};

class KeyboardLayoutVelocityDrums : public KeyboardLayout {
public:
	KeyboardLayoutVelocityDrums() = default;
	~KeyboardLayoutVelocityDrums() override = default;
	void precalculate() override {}

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_VELOCITY_DRUMS; }
	bool supportsInstrument() override { return false; }
	bool supportsKit() override { return true; }

private:
	inline uint8_t velocityFromCoords(int32_t x, int32_t y, uint32_t edge_size_x, uint32_t edge_size_y) {
		uint32_t velocity = 0;
		if (edge_size_x == 1) {
			// No need to do a lot of calculations or use max velocity for only one option.
			velocity = FlashStorage::defaultVelocity * 2;
		}
		else {
			bool odd_pad = (edge_size_x % 2 == 1);              // check if the view has odd width pads
			uint32_t x_limit = kDisplayWidth - 2 - edge_size_x; // end of second to last pad in a row (the regular pads)
			bool x_adjust = (odd_pad && x > x_limit);
			uint32_t localX = x_adjust ? x - x_limit : x % (edge_size_x);

			if (edge_size_y == 1) {
				velocity = (localX + 1) * 200 / (edge_size_x + x_adjust); // simpler, more useful, easier on the ears.
			}
			else {
				if (edge_size_x % 2 == 1 && x > kDisplayWidth - 2 - edge_size_x)
					edge_size_x += 1;
				uint32_t position = localX + 1;
				position += ((y % edge_size_y) * (edge_size_x + x_adjust));
				// We use two bytes to keep the precision of the calculations high,
				// then shift it down to one byte at the end.
				uint32_t stepSize = 0xFFFF / ((edge_size_x + x_adjust) * edge_size_y);
				velocity = (position * stepSize) >> 8;
			}
		}
		return velocity; // returns an integer value 0-255, which will then be divided by 2 to get 0-127
	}
};

}; // namespace deluge::gui::ui::keyboard::layout
