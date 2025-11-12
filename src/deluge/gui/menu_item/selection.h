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
#include "gui/ui/sound_editor.h"
#include "util/containers.h"
#include <string_view>

namespace deluge::gui::menu_item {
class Selection : public Enumeration {
public:
	using Enumeration::Enumeration;

	enum class OptType { FULL, SHORT };
	virtual deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) = 0;

	void drawValue() override;

	void drawPixelsForOled() override;
	size_t size() override { return this->getOptions().size(); }

	virtual bool isToggle() { return false; }

	void displayToggleValue();

	// renders toggle item type in submenus after the item name
	void renderSubmenuItemTypeForOled(int32_t yPixel) override;

	// toggles boolean ON / OFF
	void toggleValue() {
		if (isToggle()) {
			readCurrentValue();
			setValue(!getValue());
			writeCurrentValue();
		}
	};

	// handles toggling a "toggle" selection menu from sub-menu level
	// or handles going back up a level after making a selection from within selection menu
	MenuItem* selectButtonPress() override {
		// this is true if you open a selection menu using grid shortcut
		// or you enter a selection menu that isn't a toggle
		if (soundEditor.getCurrentMenuItem() == this) {
			return nullptr; // go up a level
		}
		// you're toggling selection menu from submenu level
		toggleValue();
		displayToggleValue();
		return NO_NAVIGATION;
	}

	// get's toggle status for rendering checkbox on OLED
	bool getToggleValue() {
		readCurrentValue();
		return this->getValue();
	}

	// get's toggle status for rendering dot on 7SEG
	uint8_t shouldDrawDotOnName() override {
		if (isToggle()) {
			readCurrentValue();
			return this->getValue() ? 3 : 255;
		}
		else {
			return 255;
		}
	}

protected:
	void getShortOption(StringBuf&) override;
	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override;
};
} // namespace deluge::gui::menu_item
