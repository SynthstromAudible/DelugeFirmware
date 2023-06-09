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

#ifndef CONTEXTMENU_H_
#define CONTEXTMENU_H_

#include "UI.h"
#include "r_typedefs.h"

class ContextMenu : public UI {
public:
	ContextMenu();
	void focusRegained();
	void selectEncoderAction(int8_t offset);
	int buttonAction(int x, int y, bool on, bool inCardRoutine) final;
	void drawCurrentOption();
	virtual int getNumOptions() { return basicNumOptions; }
	virtual bool isCurrentOptionAvailable() { return true; }
	virtual bool acceptCurrentOption() { return false; } // If returns false, will cause UI to exit
	virtual char const** getOptions();
	bool getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows);
	int padAction(int x, int y, int velocity);
	bool setupAndCheckAvailability();

	virtual int getAcceptButtonX() { return selectEncButtonX; }
	virtual int getAcceptButtonY() { return selectEncButtonY; }

	int currentOption; // Don't make static. We'll have multiple nested ContextMenus open at the same time

	char const** basicOptions;
	int basicNumOptions;
#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
	int scrollPos; // Don't make static. We'll have multiple nested ContextMenus open at the same time
	char const* title;
#endif
};

class ContextMenuForSaving : public ContextMenu {
public:
	ContextMenuForSaving() {}
	void focusRegained() final;
	virtual int getAcceptButtonX() final { return saveButtonX; }
	virtual int getAcceptButtonY() final { return saveButtonY; }
};

class ContextMenuForLoading : public ContextMenu {
public:
	ContextMenuForLoading() {}
	void focusRegained();
	virtual int getAcceptButtonX() final { return loadButtonX; }
	virtual int getAcceptButtonY() final { return loadButtonY; }
};

#endif /* CONTEXTMENU_H_ */
