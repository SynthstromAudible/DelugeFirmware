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

#ifndef MENUITEMRANGE_H_
#define MENUITEMRANGE_H_

#include "MenuItem.h"

#define RANGE_EDIT_OFF 0
#define RANGE_EDIT_LEFT 1
#define RANGE_EDIT_RIGHT 2

class MenuItemRange : public MenuItem {
public:
	MenuItemRange(char const* newName = NULL) : MenuItem(newName){};

	void beginSession(MenuItem* navigatedBackwardFrom);
	void horizontalEncoderAction(int offset) final;
	bool cancelEditingIfItsOn();

protected:
	virtual void getText(char* buffer, int* getLeftLength = NULL, int* getRightLength = NULL,
	                     bool mayShowJustOne = true) = 0;
	virtual bool mayEditRangeEdge(int whichEdge) { return true; }
	void drawValue(int startPos = 0, bool renderSidebarToo = true);
	void drawValueForEditingRange(bool blinkImmediately);

#if HAVE_OLED
	void drawPixelsForOled();
#endif
};

#endif /* MENUITEMRANGE_H_ */
