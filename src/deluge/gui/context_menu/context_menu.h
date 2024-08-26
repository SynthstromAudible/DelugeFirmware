/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/ui/ui.h"
#include "hid/button.h"
#include <cstdint>
#include <span>

namespace deluge::gui {

class ContextMenu : public UI {
public:
	ContextMenu() { oledShowsUIUnderneath = true; };

	virtual ~ContextMenu() = default;

	void focusRegained() override;
	void selectEncoderAction(int8_t offset) override;
	virtual ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	void drawCurrentOption();
	virtual bool isCurrentOptionAvailable() { return true; }
	virtual bool acceptCurrentOption() { return false; } // If returns false, will cause UI to exit

	virtual std::span<char const*> getOptions() = 0;

	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	bool setupAndCheckAvailability();

	virtual deluge::hid::Button getAcceptButton() { return deluge::hid::button::SELECT_ENC; }

	int32_t currentOption = 0; // Don't make static. We'll have multiple nested ContextMenus open at the same time

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	int32_t scrollPos = 0; // Don't make static. We'll have multiple nested ContextMenus open at the same time
	virtual char const* getTitle() = 0;

	// UI

	// NB! Context menus all share a type!
	UIType getUIType() override { return UIType::CONTEXT_MENU; }
};

class ContextMenuForSaving : public ContextMenu {
public:
	ContextMenuForSaving() = default;
	~ContextMenuForSaving() override = default;

	void focusRegained() final;
	deluge::hid::Button getAcceptButton() final { return deluge::hid::button::SAVE; }
};

class ContextMenuForLoading : public ContextMenu {
public:
	ContextMenuForLoading() = default;
	~ContextMenuForLoading() override = default;

	void focusRegained() override;
	deluge::hid::Button getAcceptButton() final { return deluge::hid::button::LOAD; }
};
} // namespace deluge::gui
