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
#include <stdint.h>

#include "decimal.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "util/cfunctions.h"
#include "util/functions.h"

#include "io/debug/log.h"

namespace deluge::gui::menu_item {

void Decimal::beginSession(MenuItem* navigatedBackwardFrom) {
	setupNumberEditor();
	readCurrentValue();
	scrollToGoodPos();
	drawValue();
}

void Decimal::setupNumberEditor() {
	soundEditor.numberScrollAmount = 0;
	soundEditor.numberEditPos = getDefaultEditPos();
	soundEditor.numberEditSize = 1;
	for (int32_t i = 0; i < soundEditor.numberEditPos; i++) {
		soundEditor.numberEditSize *= 10;
	}
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
	int32_t numDecimalPlaces = getNumDecimalPlaces();
	char buffer[13];
	intToString(this->getValue(), buffer, numDecimalPlaces + 1);
	int32_t length = strlen(buffer);

	int32_t editingChar = length - soundEditor.numberEditPos;
	if (soundEditor.numberEditPos >= numDecimalPlaces) {
		editingChar--;
	}

	if (numDecimalPlaces) {
		int32_t numCharsBeforeDecimalPoint = length - numDecimalPlaces;
		memmove(&buffer[numCharsBeforeDecimalPoint + 1], &buffer[numCharsBeforeDecimalPoint], numDecimalPlaces + 1);
		buffer[numCharsBeforeDecimalPoint] = '.';
		length++;
	}

	int32_t digitWidth = kTextHugeSpacingX;
	int32_t stringWidth = digitWidth * length;
	int32_t stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	hid::display::OLED::main.drawString(buffer, stringStartX, 20, digitWidth, kTextHugeSizeY);

	int32_t ourDigitStartX = stringStartX + editingChar * digitWidth;
	hid::display::OLED::setupBlink(ourDigitStartX, digitWidth, 40, 44, movingCursor);
}

void Decimal::drawActualValue(bool justDidHorizontalScroll) {
	char buffer[12];
	int32_t minNumDigits = getNumDecimalPlaces() + 1;
	minNumDigits = std::max<int32_t>(minNumDigits, soundEditor.numberEditPos + 1);
	intToString(this->getValue(), buffer, minNumDigits);
	int32_t stringLength = strlen(buffer);

	char* outputText = buffer + std::max(stringLength - 4 - soundEditor.numberScrollAmount, 0_i32);

	if (strlen(outputText) > 4) {
		outputText[4] = 0;
	}

	int32_t dotPos;
	if (getNumDecimalPlaces())
		dotPos = soundEditor.numberScrollAmount + 3 - getNumDecimalPlaces();
	else
		dotPos = 255;

	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !justDidHorizontalScroll);

	uint8_t blinkMask[kNumericDisplayLength];
	memset(&blinkMask, 255, kNumericDisplayLength);
	blinkMask[3 + soundEditor.numberScrollAmount - soundEditor.numberEditPos] = 0b10000000;

	display->setText(outputText,
	                 true,   // alignRight
	                 dotPos, // drawDot
	                 true,   // doBlink
	                 blinkMask,
	                 false); // blinkImmediately
}

// TODO: pretty-much identical with PatchCableStrenth version
void Decimal::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;

	std::string_view name = getName();
	size_t len = std::min((size_t)(width / kTextSpacingX), name.size());
	// If we can fit the whole name, we do, if we can't we chop one letter off. It just looks and
	// feels better, at least with the names we have now.
	if (name.size() > len) {
		len -= 1;
	}
	std::string_view shortName(name.data(), len);
	image.drawString(shortName, startX, startY, kTextSpacingX, kTextSpacingY, 0, startX + width);

	DEF_STACK_STRING_BUF(paramValue, 10);
	float value = getValue() / (float)getEditorScale();
	if (isDisabledBelowZero() && value < 0) {
		paramValue.append("OFF");
	}
	else {
		int32_t d = getNumDecimalPlaces();
		paramValue.appendFloat(value, d, d);
	}
	int32_t pxLen;
	// Trim characters from the end until it fits.
	while ((pxLen = image.getStringWidthInPixels(paramValue.c_str(), kTextTitleSizeY)) >= width) {
		paramValue.data()[paramValue.size() - 1] = 0;
	}
	// Padding to center the string. If we can't center exactly, 1px right is better than 1px left.
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(paramValue.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY,
	                 0, startX + width);
}

} // namespace deluge::gui::menu_item
