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
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"

#include <gui/menu_item/horizontal_menu.h>

namespace deluge::gui::menu_item::arpeggiator {
class Randomizer final : public HorizontalMenu {
public:
	using HorizontalMenu::HorizontalMenu;

	[[nodiscard]] bool showColumnLabel() const override { return false; }
	[[nodiscard]] int32_t getColumnSpan() const override { return 1; }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		// Draw dice icon
		const auto& diceIcon = OLED::diceIcon;
		const int32_t diceIconWidth = diceIcon.size() / 2;
		constexpr int32_t diceIconHeight = 16;

		int32_t x = startX + 6;
		int32_t y = startY + (height - diceIconHeight) / 2 + 1;
		image.drawGraphicMultiLine(diceIcon.data(), x, y, diceIconWidth, diceIconHeight, 2);

		// Draw arrow
		const auto& arrowIcon = OLED::submenuArrowIconBold;
		constexpr int32_t arrowIconWidth = 7;
		constexpr int32_t arrowIconHeight = 8;
		x += diceIconWidth + 1;
		y = startY + (height - arrowIconHeight) / 2;
		image.drawGraphicMultiLine(arrowIcon, x, y, arrowIconWidth);
	}
};

} // namespace deluge::gui::menu_item::arpeggiator
