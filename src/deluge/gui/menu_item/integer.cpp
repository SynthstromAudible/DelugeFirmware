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
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include <cstring>

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "util/cfunctions.h"
}

namespace menu_item {

void Integer::selectEncoderAction(int offset) {
	soundEditor.currentValue += offset;
	int maxValue = getMaxValue();
	if (soundEditor.currentValue > maxValue) {
		soundEditor.currentValue = maxValue;
	}
	else {
		int minValue = getMinValue();
		if (soundEditor.currentValue < minValue) {
			soundEditor.currentValue = minValue;
		}
	}

	Number::selectEncoderAction(offset);
}

#if !HAVE_OLED
void Integer::drawValue() {
	numericDriver.setTextAsNumber(soundEditor.currentValue);
}

void IntegerWithOff::drawValue() {
	if (soundEditor.currentValue == 0) {
		numericDriver.setText("OFF");
	}
	else {
		Integer::drawValue();
	}
}
#endif

#if HAVE_OLED
void Integer::drawInteger(int textWidth, int textHeight, int yPixel) {
	char buffer[12];
	intToString(soundEditor.currentValue, buffer, 1);
	OLED::drawStringCentred(buffer, yPixel + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
	                        textWidth, textHeight);
}

void Integer::drawPixelsForOled() {
	drawInteger(TEXT_HUGE_SPACING_X, TEXT_HUGE_SIZE_Y, 18);
}

void IntegerContinuous::drawPixelsForOled() {

#if OLED_MAIN_HEIGHT_PIXELS == 64
	drawInteger(13, 15, 20);
#else
	drawInteger(TEXT_BIG_SPACING_X, TEXT_BIG_SIZE_Y, 15);
#endif

	drawBar(35, 10);
}
#endif
} // namespace menu_item
