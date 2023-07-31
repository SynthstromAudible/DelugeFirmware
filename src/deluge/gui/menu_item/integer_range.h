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

namespace deluge::gui::menu_item {

class IntegerRange final : public Range {
public:
	IntegerRange(const string& newName, const string& title, int newMin, int newMax)
	    : Range(newName, title), minValue(newMin), maxValue(newMax) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void getText(char* buffer, int* getLeftLength, int* getRightLength, bool mayShowJustOne) override;
	void selectEncoderAction(int offset) override;
	int getRandomValueInRange();

	int lower, upper;

	int minValue, maxValue;
};
} // namespace deluge::gui::menu_item
