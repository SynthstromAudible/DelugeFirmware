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

		const int32_t pixelSize = 3;
		int32_t maxCellsX = (width - 6) / pixelSize;
		int32_t maxCellsY = (height - 4) / pixelSize;

		// Force odd number of cells for symmetry
		if (maxCellsX % 2 == 0)
			maxCellsX--;
		if (maxCellsY % 2 == 0)
			maxCellsY--;

		int32_t boxWidth = maxCellsX * pixelSize;
		int32_t boxHeight = maxCellsY * pixelSize;
		int32_t boxLeft = startX + (width - boxWidth) / 2;
		int32_t boxTop = startY + (height - boxHeight) / 2;

		// Draw checkerboard with larger "pixels"
		for (int cellY = 0; cellY < maxCellsY; ++cellY) {
			for (int cellX = 0; cellX < maxCellsX; ++cellX) {
				if ((cellX + cellY) % 2 == 1) {
					for (int dy = 0; dy < pixelSize; ++dy) {
						for (int dx = 0; dx < pixelSize; ++dx) {
							canvas.drawPixel(boxLeft + cellX * pixelSize + dx, boxTop + cellY * pixelSize + dy);
						}
					}
				}
			}
		}
	}
};

} // namespace deluge::gui::menu_item
