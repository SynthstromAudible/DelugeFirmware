/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

namespace menu_item::patched_param {
class Pan : public Integer {
public:
	Pan(char const* newName = 0, int newP = 0) : Integer(newName, newP) {}
#if !HAVE_OLED
	void drawValue();
#endif
protected:
	int getMaxValue() {
		return 32;
	}
	int getMinValue() {
		return -32;
	}
	int32_t getFinalValue();
	void readCurrentValue();
};
} // namespace menu_item::patched_param
