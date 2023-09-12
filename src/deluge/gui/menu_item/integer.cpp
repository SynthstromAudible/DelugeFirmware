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
#include "hid/display/numeric_driver.h"
#include "hid/buttons.h"
#include <cstring>

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

extern "C" {
#include "util/cfunctions.h"
}

namespace deluge::gui::menu_item {

void Integer::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	if (b == hid::button::Y_ENC) {
		if (on) {
			int32_t newValue = rand() % (getMaxValue() - getMinValue() + 1) + getMinValue();
			int32_t derivedOffset = newValue - this->getValue();
			this->setValue(newValue);
			Number::selectEncoderAction(derivedOffset);
		}
	}

	// handle revert to value when beginSession was called NON-WORKING {
	if (b == hid::button::X_ENC) {
		if (on) {
			int32_t derivedOffset = this->getOriginalValue() - this->getValue();
			this->resetToOriginalValue();
			Number::selectEncoderAction(derivedOffset);
		}
	}
	// }

}

void Integer::selectEncoderAction(int32_t offset) {
	this->setValue(this->getValue() + offset);
	int32_t maxValue = getMaxValue();
	if (this->getValue() > maxValue) {
		this->setValue(maxValue);
	}
	else {
		int32_t minValue = getMinValue();
		if (this->getValue() < minValue) {
			this->setValue(minValue);
		}
	}

	Number::selectEncoderAction(offset);
}

#if !HAVE_OLED
void Integer::drawValue() {
	numericDriver.setTextAsNumber(this->getValue());
}

void IntegerWithOff::drawValue() {
	if (this->getValue() == 0) {
		numericDriver.setText("OFF");
	}
	else {
		Integer::drawValue();
	}
}
#endif

#if HAVE_OLED
void Integer::drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
	char buffer[12];
	intToString(this->getValue(), buffer, 1);
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
#endif
} // namespace deluge::gui::menu_item
