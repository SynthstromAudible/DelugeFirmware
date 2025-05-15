/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "hid/display/oled.h"
#include "menu_item.h"

namespace deluge::gui::menu_item {

class Dummy final : public MenuItem {
public:
	using MenuItem::MenuItem;

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;

		const int32_t margin = 4;
		int32_t maxSquare = std::min(width, height) - 2 * margin;

		// Force odd size for better dotted border appearance
		if (maxSquare % 2 == 0)
			maxSquare--;

		int32_t boxSize = maxSquare;
		int32_t boxLeft = startX + (width - boxSize) / 2;
		int32_t boxTop = startY + (height - boxSize) / 2;
		int32_t boxRight = boxLeft + boxSize - 1;
		int32_t boxBottom = boxTop + boxSize - 1;

		// Draw dotted square border
		for (int x = boxLeft; x <= boxRight; ++x) {
			if ((x - boxLeft) % 2 == 0) {
				canvas.drawPixel(x, boxTop);
				canvas.drawPixel(x, boxBottom);
			}
		}
		for (int y = boxTop; y <= boxBottom; ++y) {
			if ((y - boxTop) % 2 == 0) {
				canvas.drawPixel(boxLeft, y);
				canvas.drawPixel(boxRight, y);
			}
		}
	}
};

} // namespace deluge::gui::menu_item
