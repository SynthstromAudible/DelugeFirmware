/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "number.h"

namespace menu_item {

class Integer : public Number {
public:
	using Number::Number;
	void selectEncoderAction(int offset) override;

protected:
	// OLED Only
	void drawPixelsForOled();
	virtual void drawInteger(int textWidth, int textHeight, int yPixel);

	// 7Seg Only
	void drawValue() override;
};

class IntegerWithOff : public Integer {
public:
	using Integer::Integer;

	// 7Seg Only
	void drawValue() override;
};

class IntegerContinuous : public Integer {
public:
	using Integer::Integer;

protected:
	// OLED Only
	void drawPixelsForOled();
};

} // namespace menu_item
