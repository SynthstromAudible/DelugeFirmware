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

#include "integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.hpp"
#include <cstring>

extern "C" {
#include "util/cfunctions.h"
}

namespace deluge::gui::menu_item {

void Integer::selectEncoderAction(int32_t offset) {
	this->value_ += offset;
	int32_t maxValue = getMaxValue();
	if (this->value_ > maxValue) {
		this->value_ = maxValue;
	}
	else {
		int32_t minValue = getMinValue();
		if (this->value_ < minValue) {
			this->value_ = minValue;
		}
	}

	Number::selectEncoderAction(offset);
}

void Integer::drawValue() {
	display.setTextAsNumber(this->value_);
}

void IntegerWithOff::drawValue() {
	if (this->value_ == 0) {
		display.setText("OFF");
	}
	else {
		Integer::drawValue();
	}
}

void Integer::drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
	char buffer[12];
	intToString(this->value_, buffer, 1);
	OLED::drawStringCentred(buffer, yPixel + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                        textWidth, textHeight);
}

void Integer::drawPixelsForOled() {
	drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18);
}

void IntegerContinuous::drawPixelsForOled() {

#if OLED_MAIN_HEIGHT_PIXELS == 64
	drawInteger(13, 15, 20);
#else
	drawInteger(kTextBigSpacingX, kTextBigSizeY, 15);
#endif

	drawBar(35, 10);
}
} // namespace deluge::gui::menu_item
