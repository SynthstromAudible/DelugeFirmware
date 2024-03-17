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

class DxPatch;

namespace deluge::gui::menu_item {

class DxParam final : public MenuItem {
public:
	using MenuItem::MenuItem;
	DxParam(l10n::String newName) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void drawPixelsForOled() override;
	void flashParamName();
	void readValueAgain() final;
	void selectEncoderAction(int32_t offset) final;
	void drawValue();
	void horizontalEncoderAction(int32_t offset) override;
	ActionResult timerCallback() override;
	[[nodiscard]] std::string_view getTitle() const override;

	bool potentialShortcutPadAction(int32_t x, int32_t y, bool on);

	void setValue(int val);
	int getValue();

	// 0-5: operators, 6: global params
	void openForOpOrGlobal(int op);
	void blinkSideColumn();
	bool hasSideColumn();

	int param = 0;
	int upper_limit = 0;
	int32_t displayValue = 0;
	DxPatch* patch;

	int flash_row = -1;
	bool blink_next = false;
};

extern DxParam dxParam;
} // namespace deluge::gui::menu_item
