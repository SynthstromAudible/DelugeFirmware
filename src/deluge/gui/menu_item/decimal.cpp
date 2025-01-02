/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "decimal.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "util/d_string.h"
#include "util/functions.h"
#include "util/string.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace deluge::gui::menu_item {

void Decimal::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.numberScrollAmount = 0;
	soundEditor.numberEditPos = getDefaultEditPos();
	soundEditor.numberEditSize = 1;

	for (int32_t i = 0; i < soundEditor.numberEditPos; i++) {
		soundEditor.numberEditSize *= 10;
	}

	readCurrentValue();
	scrollToGoodPos();
	drawValue();
}

void Decimal::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawActualValue();
	}
}

void Decimal::selectEncoderAction(int32_t offset) {

	this->setValue(this->getValue() + offset * soundEditor.numberEditSize);

	// If turned down
	if (offset < 0) {
		int32_t minValue = getMinValue();
		if (this->getValue() < minValue) {
			this->setValue(minValue);
		}
	}

	// If turned up
	else {
		int32_t maxValue = getMaxValue();
		if (this->getValue() > maxValue) {
			this->setValue(maxValue);
		}
	}

	scrollToGoodPos();

	Number::selectEncoderAction(offset);
}

bool movingCursor = false; // Sorry, ugly hack.

void Decimal::horizontalEncoderAction(int32_t offset) {
	if (offset == 1) {
		if (soundEditor.numberEditPos > 0) {
			soundEditor.numberEditPos--;
			soundEditor.numberEditSize /= 10;
		}
	}

	else {
		if (soundEditor.numberEditSize * 10 <= getMaxValue()) {
			soundEditor.numberEditPos++;
			soundEditor.numberEditSize *= 10;
		}
	}

	if (display->haveOLED()) {
		movingCursor = true;
		renderUIsForOled();
		movingCursor = false;
	}
	else {
		scrollToGoodPos();
		drawActualValue(true);
	}
}

void Decimal::scrollToGoodPos() {
	int32_t numDigits = getNumDecimalDigits(std::abs(this->getValue()));

	// Negative numbers
	if (this->getValue() < 0) {
		soundEditor.numberScrollAmount = std::max<int32_t>(numDigits - 3, soundEditor.numberEditPos - 2);
	}

	// Positive numbers
	else {
		soundEditor.numberScrollAmount = std::max<int32_t>(numDigits - 4, soundEditor.numberEditPos - 3);
	}

	if (soundEditor.numberScrollAmount < 0) {
		soundEditor.numberScrollAmount = 0;
	}

	if (soundEditor.numberEditPos > soundEditor.numberScrollAmount + 3) {
		soundEditor.numberScrollAmount = soundEditor.numberEditPos - 3;
	}
	else if (soundEditor.numberEditPos < soundEditor.numberScrollAmount) {
		soundEditor.numberScrollAmount = soundEditor.numberEditPos;
	}
}

void Decimal::drawPixelsForOled() {
	int32_t fractional_length = getNumDecimalPlaces();

	std::string int_string = string::fromInt(this->getValue(), fractional_length + 1);

	std::string_view int_string_view = int_string;

	int32_t editingChar = int_string.length() - soundEditor.numberEditPos;
	if (soundEditor.numberEditPos >= fractional_length) {
		editingChar--;
	}

	int32_t digitWidth = kTextHugeSpacingX;
	int32_t periodWidth = digitWidth / 2;
	int32_t stringWidth = (digitWidth * int_string.length()) + (fractional_length ? periodWidth : 0);
	int32_t stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;
	int32_t ourDigitStartX = stringStartX + editingChar * digitWidth;

	// for values with decimal digits showing
	if (fractional_length) {
		int32_t integral_length = int_string.length() - fractional_length;

		// draw digits before period
		hid::display::OLED::main.drawString(int_string_view.substr(0, integral_length), stringStartX, 20, digitWidth,
		                                    kTextHugeSizeY, 0, 128, true);
		// draw period
		hid::display::OLED::main.drawString(".", stringStartX + integral_length * digitWidth, 20, periodWidth,
		                                    kTextHugeSizeY, 0, 128, true);

		// modify properties so that the remaining digits and the cursor get drawn correctly
		int_string_view = int_string_view.substr(integral_length);
		stringStartX += integral_length * digitWidth + periodWidth;
		if (editingChar > integral_length)
			ourDigitStartX -= periodWidth;
	}

	// draw remaining digits
	hid::display::OLED::main.drawString(int_string_view, stringStartX, 20, digitWidth, kTextHugeSizeY, 0, 128, true);

	// draw cursor
	hid::display::OLED::setupBlink(ourDigitStartX + 1, digitWidth - 2, 41, 42, movingCursor);
}

void Decimal::drawActualValue(bool justDidHorizontalScroll) {

	int32_t minNumDigits = getNumDecimalPlaces() + 1;
	minNumDigits = std::max<int32_t>(minNumDigits, soundEditor.numberEditPos + 1);

	std::string int_string = string::fromInt(this->getValue(), minNumDigits);

	std::string_view output_text =
	    static_cast<std::string_view>(int_string)
	        .substr(std::max(static_cast<int32_t>(int_string.length()) - 4 - soundEditor.numberScrollAmount, 0_i32));

	if (output_text.length() > 4) {
		output_text = output_text.substr(0, 4);
	}

	int32_t dotPos;
	if (getNumDecimalPlaces() != 0) {
		dotPos = soundEditor.numberScrollAmount + 3 - getNumDecimalPlaces();
	}
	else {
		dotPos = 255;
	}

	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !justDidHorizontalScroll);

	uint8_t blinkMask[kNumericDisplayLength];
	memset(&blinkMask, 255, kNumericDisplayLength);
	blinkMask[3 + soundEditor.numberScrollAmount - soundEditor.numberEditPos] = 0b10000000;

	display->setText(output_text,
	                 true,   // alignRight
	                 dotPos, // drawDot
	                 true,   // doBlink
	                 blinkMask,
	                 false); // blinkImmediately
}

void DecimalWithoutScrolling::selectEncoderAction(int32_t offset) {
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

void DecimalWithoutScrolling::drawDecimal(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
	int32_t numDecimalPlaces = getNumDecimalPlaces();

	std::string float_string = string::fromFloat(getDisplayValue(), numDecimalPlaces);
	float_string.append(getUnit(), 4);

	deluge::hid::display::OLED::main.drawStringCentred(float_string, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth,
	                                                   textHeight);
}

void DecimalWithoutScrolling::drawPixelsForOled() {
	drawDecimal(kTextHugeSpacingX, kTextHugeSizeY, 18);
}

void DecimalWithoutScrolling::drawActualValue(bool justDidHorizontalScroll) {
	int32_t dotPos;
	float displayValue = getDisplayValue();
	int32_t numDecimalPlaces = displayValue > 100 ? 1 : 2;
	std::string text = string::fromFloat(displayValue, numDecimalPlaces);
	if (numDecimalPlaces) {
		dotPos = 3 - numDecimalPlaces;
	}
	else {
		dotPos = 255;
	}
	display->setText(text, dotPos);
}
} // namespace deluge::gui::menu_item
