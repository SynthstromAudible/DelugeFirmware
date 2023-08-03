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

#include "range.h"

namespace menu_item {

class IntegerRange final : public Range {
public:
	IntegerRange(char const* newName = NULL, int32_t newMin = 0, int32_t newMax = 0);
	void beginSession(MenuItem* navigatedBackwardFrom);
	void getText(char* buffer, int32_t* getLeftLength, int32_t* getRightLength, bool mayShowJustOne);
	void selectEncoderAction(int32_t offset);
	int32_t getRandomValueInRange();

	int32_t lower, upper;

	int32_t minValue, maxValue;
};
} // namespace menu_item
