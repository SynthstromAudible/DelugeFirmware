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

#include "gui/menu_item/enumeration.h"
#include "util/containers.h"
#include <string_view>

namespace deluge::gui::menu_item {
class Selection : public Enumeration {
public:
	using Enumeration::Enumeration;

	virtual deluge::vector<std::string_view> getOptions() = 0;

	void drawValue() override;

	void drawPixelsForOled() override;
	size_t size() override { return this->getOptions().size(); }

	virtual bool isToggle() { return false; }

	void displayToggleValue();

	// renders toggle item type in submenus after the item name
	void renderSubmenuItemTypeForOled(int32_t xPixel, int32_t yPixel) override;

	// toggles boolean ON / OFF
	void toggleValue() {
		readCurrentValue();
		setValue(!getValue());
		writeCurrentValue();
	};

	// handles changing bool setting without entering menu and updating the display
	MenuItem* selectButtonPress() override {
		toggleValue();
		displayToggleValue();
		return (MenuItem*)0xFFFFFFFF; // no navigation
	}

	// get's toggle status for rendering checkbox on OLED
	bool getToggleValue() {
		readCurrentValue();
		return this->getValue();
	}

	// get's toggle status for rendering dot on 7SEG
	uint8_t shouldDrawDotOnName() override {
		readCurrentValue();
		return this->getValue() ? 3 : 255;
	}
};
} // namespace deluge::gui::menu_item
