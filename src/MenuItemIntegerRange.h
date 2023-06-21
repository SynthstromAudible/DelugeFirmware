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

#ifndef MENUITEMINTEGERRANGE_H_
#define MENUITEMINTEGERRANGE_H_

#include "MenuItemRange.h"

class MenuItemIntegerRange final : public MenuItemRange {
public:
	MenuItemIntegerRange(char const* newName = NULL) : MenuItemRange(newName) {}
	void init(char const* newName = NULL, int newMin = 0, int newMax = 0) {
		name = newName;
		minValue = newMin;
		maxValue = newMax;
	}
	void beginSession(MenuItem* navigatedBackwardFrom);
	void getText(char* buffer, int* getLeftLength, int* getRightLength, bool mayShowJustOne);
	void selectEncoderAction(int offset);
	int getRandomValueInRange();

	int lower, upper;

	int minValue, maxValue;
};

#endif /* MENUITEMINTEGERRANGE_H_ */
