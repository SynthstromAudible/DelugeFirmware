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

#ifndef NUMERICLAYERBASICTEXT_H_
#define NUMERICLAYERBASICTEXT_H_

#include "hid/display/numeric_layer/numeric_layer.h"

class NumericLayerBasicText final : public NumericLayer {
public:
	NumericLayerBasicText();
	virtual ~NumericLayerBasicText();

	void render(uint8_t* returnSegments);
	void renderWithoutBlink(uint8_t* returnSegments);
	bool callBack();
	void isNowOnTop();

	uint8_t segments[NUMERIC_DISPLAY_LENGTH];
	uint8_t blinkedSegments[NUMERIC_DISPLAY_LENGTH];

	bool currentlyBlanked;

	int8_t blinkCount;

	int8_t blinkSpeed;
};

#endif /* NUMERICLAYERBASICTEXT_H_ */
