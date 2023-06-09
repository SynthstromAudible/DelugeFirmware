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

#ifndef MENUITEMMULTIRANGE_H_
#define MENUITEMMULTIRANGE_H_

#include "MenuItemRange.h"

class MenuItemMultiRange final : public MenuItemRange {
public:
	MenuItemMultiRange();

	void beginSession(MenuItem* navigatedBackwardFrom);
	void selectEncoderAction(int offset);
	MenuItem* selectButtonPress();
	void noteOnToChangeRange(int noteCode);
	bool isRangeDependent() { return true; }
	void deletePress();
	MenuItem* menuItemHeadingTo;

protected:
	void getText(char* buffer, int* getLeftLength = NULL, int* getRightLength = NULL, bool mayShowJustOne = true);
	bool mayEditRangeEdge(int whichEdge);

#if HAVE_OLED
	void drawPixelsForOled();
#endif
};

extern MenuItemMultiRange multiRangeMenu;

#endif /* MENUITEMMULTIRANGE_H_ */
