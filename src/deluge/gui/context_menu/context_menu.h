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

#include <cstddef>

#include "gui/ui/ui.h"
#include "hid/button.h"
#include "RZA1/system/r_typedefs.h"
#include "util/sized.h"

namespace deluge::gui {

class ContextMenu : public UI {
public:
	ContextMenu();
	void focusRegained() override;
	void selectEncoderAction(int8_t offset) override;
	int buttonAction(hid::Button b, bool on, bool inCardRoutine) final;
	void drawCurrentOption();
	virtual bool isCurrentOptionAvailable() { return true; }
	virtual bool acceptCurrentOption() { return false; } // If returns false, will cause UI to exit

	virtual Sized<char const**> getOptions() = 0;

	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) override;
	int padAction(int x, int y, int velocity) override;
	bool setupAndCheckAvailability();

	virtual hid::Button getAcceptButton() { return hid::button::SELECT_ENC; }

	int currentOption; // Don't make static. We'll have multiple nested ContextMenus open at the same time

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
	int scrollPos; // Don't make static. We'll have multiple nested ContextMenus open at the same time
#endif
	virtual char const* getTitle() = 0;
};

class ContextMenuForSaving : public ContextMenu {
public:
	ContextMenuForSaving() = default;
	void focusRegained() final;
	hid::Button getAcceptButton() final { return hid::button::SAVE; }
};

class ContextMenuForLoading : public ContextMenu {
public:
	ContextMenuForLoading() = default;
	void focusRegained() override;
	hid::Button getAcceptButton() final { return hid::button::LOAD; }
};
} // namespace deluge::gui
