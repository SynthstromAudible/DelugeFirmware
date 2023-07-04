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

#include "integer.h"

class ParamSet;
class ModelStackWithAutoParam;

namespace menu_item {

// Note that this does *not* inherit from MenuItem actually!
class Param {
public:
	Param(int newP = 0) { p = newP; }
	int getMaxValue() const { return 50; }
	int getMinValue() const { return 0; }
	virtual uint8_t getP() { return p; };
	MenuItem* selectButtonPress();
	virtual ModelStackWithAutoParam* getModelStack(void* memory) = 0;

	uint8_t p;

protected:
	virtual ParamSet* getParamSet() = 0;
};
} // namespace menu_item
