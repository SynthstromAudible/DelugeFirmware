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

#include <cmath>
#include <cstring>

#include "decimal.h"
#include "source_selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "hid/display/oled.h"
#include "hid/matrix/matrix_driver.h"
#include "util/functions.h"
#include "hid/led/indicator_leds.h"

extern "C" {
#include "util/cfunctions.h"
}

namespace deluge::gui::menu_item {

void Decimal::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.numberScrollAmount = 0;
	soundEditor.numberEditPos = getDefaultEditPos();
	soundEditor.numberEditSize = 1;

	for (int i = 0; i < soundEditor.numberEditPos; i++) {
		soundEditor.numberEditSize *= 10;
	}

	readCurrentValue();
	scrollToGoodPos();
#if 1 || HAVE_OLED
	drawValue();
#endif
}

void Decimal::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	drawActualValue();
#endif
}

void Decimal::selectEncoderAction(int offset) {

	this->value_ += offset * soundEditor.numberEditSize;

	// If turned down
	if (offset < 0) {
		int minValue = getMinValue();
		if (this->value_ < minValue) {
			this->value_ = minValue;
		}
	}

	// If turned up
	else {
		int maxValue = getMaxValue();
		if (this->value_ > maxValue) {
			this->value_ = maxValue;
		}
	}

	scrollToGoodPos();

	Number::selectEncoderAction(offset);
}

bool movingCursor = false; // Sorry, ugly hack.

void Decimal::horizontalEncoderAction(int offset) {
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

#if HAVE_OLED
	movingCursor = true;
	renderUIsForOled();
	movingCursor = false;
#else
	scrollToGoodPos();
	drawActualValue(true);
#endif
}

void Decimal::scrollToGoodPos() {
	int numDigits = getNumDecimalDigits(std::abs(this->value_));

	// Negative numbers
	if (this->value_ < 0) {
		soundEditor.numberScrollAmount = getMax(numDigits - 3, soundEditor.numberEditPos - 2);
	}

	// Positive numbers
	else {
		soundEditor.numberScrollAmount = getMax(numDigits - 4, soundEditor.numberEditPos - 3);
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

#if HAVE_OLED
void Decimal::drawPixelsForOled() {
	int numDecimalPlaces = getNumDecimalPlaces();
	char buffer[13];
	intToString(this->value_, buffer, numDecimalPlaces + 1);
	int length = strlen(buffer);

	int editingChar = length - soundEditor.numberEditPos;
	if (soundEditor.numberEditPos >= numDecimalPlaces) {
		editingChar--;
	}

	if (numDecimalPlaces) {
		int numCharsBeforeDecimalPoint = length - numDecimalPlaces;
		memmove(&buffer[numCharsBeforeDecimalPoint + 1], &buffer[numCharsBeforeDecimalPoint], numDecimalPlaces + 1);
		buffer[numCharsBeforeDecimalPoint] = '.';
		length++;
	}

	int digitWidth = kTextHugeSpacingX;
	int stringWidth = digitWidth * length;
	int stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	OLED::drawString(buffer, stringStartX, 20, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, digitWidth,
	                 kTextHugeSizeY);

	int ourDigitStartX = stringStartX + editingChar * digitWidth;
	OLED::setupBlink(ourDigitStartX, digitWidth, 40, 44, movingCursor);
}

#else
void Decimal::drawActualValue(bool justDidHorizontalScroll) {
	char buffer[12];
	int minNumDigits = getNumDecimalPlaces() + 1;
	minNumDigits = getMax(minNumDigits, soundEditor.numberEditPos + 1);
	intToString(this->value_, buffer, minNumDigits);
	int stringLength = strlen(buffer);

	char* outputText = buffer + getMax(stringLength - 4 - soundEditor.numberScrollAmount, 0);

	if (strlen(outputText) > 4) {
		outputText[4] = 0;
	}

	int dotPos;
	if (getNumDecimalPlaces())
		dotPos = soundEditor.numberScrollAmount + 3 - getNumDecimalPlaces();
	else
		dotPos = 255;

	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !justDidHorizontalScroll);

	uint8_t blinkMask[kNumericDisplayLength];
	memset(&blinkMask, 255, kNumericDisplayLength);
	blinkMask[3 + soundEditor.numberScrollAmount - soundEditor.numberEditPos] = 0b10000000;

	numericDriver.setText(outputText,
	                      true,   // alignRight
	                      dotPos, // drawDot
	                      true,   // doBlink
	                      blinkMask,
	                      false); // blinkImmediately
}
#endif

} // namespace deluge::gui::menu_item
