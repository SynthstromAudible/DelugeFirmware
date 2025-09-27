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

namespace deluge::gui::menu_item::delay {
class Amount_Unpatched final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	float getNormalizedValue() override {
		const int32_t clamped = std::clamp<int32_t>(getValue(), 0, max_value_in_horizontal_menu);
		return clamped / static_cast<float>(max_value_in_horizontal_menu);
	}

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		if (getValue() > max_value_in_horizontal_menu) {
			OLED::main.drawIconCentered(OLED::delayBarInfiniteFeedbackIcon, startX, width, startY);
		}
		else {
			drawBar(startX, startY, width, height);
		}
	}

private:
	constexpr static int32_t max_value_in_horizontal_menu = 24;
};
} // namespace deluge::gui::menu_item::delay
