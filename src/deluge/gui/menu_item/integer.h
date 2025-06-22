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

namespace deluge::gui::menu_item {

class Integer : public Number {
public:
	using Number::Number;
	void selectEncoderAction(int32_t offset) override;

protected:
	virtual int32_t getDisplayValue() { return this->getValue(); }
	virtual const char* getUnit() { return ""; }

	void drawPixelsForOled() override;
	virtual void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel);

	// 7Seg Only
	void drawValue() override;
};

class IntegerWithOff : public Integer {
public:
	using Integer::Integer;
	void drawPixelsForOled() override;

	// 7Seg Only
	void drawValue() override;
};

class IntegerContinuous : public Integer {
public:
	using Integer::Integer;

protected:
	void drawPixelsForOled() override;
};

} // namespace deluge::gui::menu_item
