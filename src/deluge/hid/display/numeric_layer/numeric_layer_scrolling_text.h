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

#include "hid/display/numeric_layer/numeric_layer.h"

class NumericLayerScrollingText final : public NumericLayer {
public:
	NumericLayerScrollingText();
	virtual ~NumericLayerScrollingText();
	void render(uint8_t* returnSegments);
	bool callBack();
	void isNowOnTop();

	uint8_t text[256];
	uint16_t length;
	int8_t currentDirection;
	int8_t currentPos;
	int16_t initialDelay;

	int scrollsCount = -1;
};
