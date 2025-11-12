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

	float normalize(int32_t value) override {
		const int32_t clamped = std::clamp<int32_t>(value, 0, max_value_in_horizontal_menu);
		return clamped / static_cast<float>(max_value_in_horizontal_menu);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		drawBar(slot);

		if (getValue() > max_value_in_horizontal_menu) {
			// Draw exclamation mark
			oled_canvas::Canvas& image = OLED::main;
			constexpr uint8_t excl_mark_width = 3;
			constexpr uint8_t excl_mark_height = 9;
			const uint8_t center_x = slot.start_x + slot.width / 2;
			const uint8_t excl_mark_start_y = slot.start_y + kHorizontalMenuSlotYOffset;
			const uint8_t excl_mark_end_y = excl_mark_start_y + excl_mark_height - 1;
			const uint8_t excl_mark_start_x = center_x - 1;

			// Fill the mark area
			for (uint8_t x = center_x - 2; x <= center_x + 2; x++) {
				for (uint8_t y = excl_mark_start_y - 1; y <= excl_mark_end_y + 1; y++) {
					image.drawPixel(x, y);
				}
			}

			image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y, excl_mark_start_y + 5);
			image.invertArea(excl_mark_start_x, excl_mark_width, excl_mark_start_y + 7, excl_mark_start_y + 8);
		}
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Integer::configureRenderingOptions(options);
		options.label = l10n::get(l10n::String::STRING_FOR_AMOUNT_SHORT);
	}

private:
	constexpr static int32_t max_value_in_horizontal_menu = 24;
};
} // namespace deluge::gui::menu_item::delay
