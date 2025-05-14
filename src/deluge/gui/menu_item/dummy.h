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

		// Center box in the given area
		const int32_t boxWidth = (width < 48) ? width - 4 : 48;
		const int32_t boxHeight = (height < 32) ? height - 4 : 32;
		const int32_t boxLeft = startX + (width - boxWidth) / 2;
		const int32_t boxTop = startY + (height - boxHeight) / 2;
		const int32_t boxRight = boxLeft + boxWidth - 1;
		const int32_t boxBottom = boxTop + boxHeight - 1;

		// Draw rectangle (inclusive coordinates)
		canvas.drawRectangle(boxLeft, boxTop, boxRight, boxBottom);

		// Draw X inside the box using pixels, with 4px margin on all sides
		const int32_t margin = 4;
		const int32_t innerWidth = boxRight - boxLeft + 1 - 2 * margin;
		const int32_t innerHeight = boxBottom - boxTop + 1 - 2 * margin;
		const int32_t squareLen = std::min(innerWidth, innerHeight);

		// Center the square inside the box
		const int32_t xStart = boxLeft + margin + (innerWidth - squareLen) / 2;
		const int32_t yStart = boxTop + margin + (innerHeight - squareLen) / 2;
		const int32_t xEnd = xStart + squareLen - 1;
		const int32_t yEnd = yStart + squareLen - 1;

		for (int i = 0; i < squareLen; ++i) {
			canvas.drawPixel(xStart + i, yStart + i); // top-left to bottom-right
			canvas.drawPixel(xStart + i, yEnd - i);   // bottom-left to top-right
		}
	}
};

} // namespace deluge::gui::menu_item
