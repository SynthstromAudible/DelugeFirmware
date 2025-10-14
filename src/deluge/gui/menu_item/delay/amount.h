/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/integer.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::delay {

using namespace hid::display;

class Amount final : public patched_param::Integer {
public:
	using Integer::Integer;

	float getNormalizedValue() override {
		const int32_t clamped = std::clamp<int32_t>(getValue(), 0, max_value_in_horizontal_menu);
		return clamped / static_cast<float>(max_value_in_horizontal_menu);
	}

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		drawBar(startX, startY, width, height);

		if (getValue() > max_value_in_horizontal_menu) {
			// Draw exclamation mark
			oled_canvas::Canvas& image = OLED::main;
			const uint8_t excl_mark_start_x = startX + 20;
			const uint8_t excl_mark_start_y = startY + 2;
			constexpr uint8_t excl_mark_width = 2;
			constexpr uint8_t x_padding = 2;

			for (uint8_t x = excl_mark_start_x - x_padding; x < excl_mark_start_x + excl_mark_width + x_padding; x++) {
				image.drawPixel(x, excl_mark_start_y);
				image.drawPixel(x, excl_mark_start_y + 8);
			}

			image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y, excl_mark_start_y + 5);
			image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y + 7, excl_mark_start_y + 8);
		}
	}

	void getColumnLabel(StringBuf& label) override { label.append(l10n::get(l10n::String::STRING_FOR_AMOUNT_SHORT)); }

private:
	constexpr static int32_t max_value_in_horizontal_menu = 24;
};
} // namespace deluge::gui::menu_item::delay
