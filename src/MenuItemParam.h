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

#ifndef MENUITEMPARAM_H_
#define MENUITEMPARAM_H_

#include "MenuItemInteger.h"

class ParamSet;
class ModelStackWithAutoParam;

// Note that this does *not* inherit from MenuItem actually!
class MenuItemParam {
public:
	MenuItemParam(int newP = 0) { p = newP; }
	int getMaxValue();
	int getMinValue();
	virtual uint8_t getP();
	MenuItem* selectButtonPress();
	virtual ModelStackWithAutoParam* getModelStack(void* memory) = 0;

	uint8_t p;

protected:
	virtual ParamSet* getParamSet() = 0;
};

#endif /* MENUITEMPARAM_H_ */
