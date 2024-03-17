
/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/menu_item.h"

namespace deluge::gui::menu_item {

class DxGlobalParams final : public MenuItem {
public:
	using MenuItem::MenuItem;
	DxGlobalParams(l10n::String newName) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	bool tryLoad(const char* path);
	void drawPixelsForOled() override;
	void readValueAgain() final;
	void selectEncoderAction(int32_t offset) final;
	MenuItem* selectButtonPress() final;
	void drawValue();

	int32_t currentValue = 0;
	int scrollPos = 0; // Each instance needs to store this separately
};

extern DxGlobalParams dxGlobalParams;
} // namespace deluge::gui::menu_item
