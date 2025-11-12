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
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "horizontal_menu.h"
#include "util/cfunctions.h"
#include "util/functions.h"

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

int32_t Decimal::getNumberEditSize() {
	if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL) {
		// In Horizontal menus we use 1.00 step by default, and 0.01 step for fine editing
		return Buttons::isAnyOfButtonsPressed({hid::button::SELECT_ENC, hid::button::SHIFT}) ? 1 : 100;
	}
	return soundEditor.numberEditSize;
}

void Decimal::selectEncoderAction(int32_t offset) {
	setValue(getValue() + offset * getNumberEditSize());

	// If turned down
	if (offset < 0) {
		int32_t minValue = getMinValue();
		if (getValue() < minValue) {
			setValue(minValue);
		}
	}

	// If turned up
	else {
		int32_t maxValue = getMaxValue();
		if (getValue() > maxValue) {
			setValue(maxValue);
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

	int32_t digitWidth = kTextHugeSpacingX;
	int32_t periodWidth = digitWidth / 2;
	int32_t stringWidth = digitWidth * length + (numDecimalPlaces ? periodWidth : 0);
	int32_t stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;
	int32_t ourDigitStartX = stringStartX + editingChar * digitWidth;

	// for values with decimal digits showing
	if (numDecimalPlaces) {
		int32_t numCharsBeforeDecimalPoint = length - numDecimalPlaces;
		// draw digits before period
		hid::display::OLED::main.drawString(std::string_view(buffer, numCharsBeforeDecimalPoint), stringStartX, 20,
		                                    digitWidth, kTextHugeSizeY, 0, 128, true);
		// draw period
		hid::display::OLED::main.drawString(".", stringStartX + numCharsBeforeDecimalPoint * digitWidth, 20,
		                                    periodWidth, kTextHugeSizeY, 0, 128, true);
		// modify properties so that the remaining digits and the cursor get drawn correctly
		std::memmove(buffer, buffer + numCharsBeforeDecimalPoint, sizeof(buffer) - numCharsBeforeDecimalPoint);
		stringStartX += numCharsBeforeDecimalPoint * digitWidth + periodWidth;
		if (editingChar > numCharsBeforeDecimalPoint)
			ourDigitStartX -= periodWidth;
	}
	// draw remaining digits
	hid::display::OLED::main.drawString(buffer, stringStartX, 20, digitWidth, kTextHugeSizeY, 0, 128, true);
	// draw cursor
	hid::display::OLED::setupBlink(ourDigitStartX + 1, digitWidth - 2, 41, 42, movingCursor);
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

	std::vector<uint8_t> dotPositions{};
	if (getNumDecimalPlaces()) {
		dotPositions.push_back(soundEditor.numberScrollAmount + 3 - getNumDecimalPlaces());
	}
	appendAdditionalDots(dotPositions);

	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !justDidHorizontalScroll);

	uint8_t blinkMask[kNumericDisplayLength];
	memset(&blinkMask, 255, kNumericDisplayLength);
	blinkMask[3 + soundEditor.numberScrollAmount - soundEditor.numberEditPos] = 0b10000000;

	display->setTextWithMultipleDots(outputText, dotPositions,
	                                 true, // alignRight
	                                 true, // doBlink
	                                 blinkMask,
	                                 false); // blinkImmediately
}

int32_t Decimal::getNumNonZeroDecimals(int32_t value) {
	const float remaining = std::abs(value / 100 - value / 100.0f);
	if (remaining == 0) {
		return 0;
	}
	DEF_STACK_STRING_BUF(remainingBuf, 8);
	remainingBuf.appendFloat(remaining, 0, 2);
	return remainingBuf.size() - 2;
}

void Decimal::renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) {
	if (getRenderingStyle() != NUMBER) {
		return Number::renderInHorizontalMenu(slot);
	}

	DEF_STACK_STRING_BUF(valueBuf, 10);
	const int32_t value = getValue();
	valueBuf.appendFloat(value / 100.f, 2, 2);
	if (value <= -1000) {
		valueBuf.truncate(3);
	}

	return hid::display::OLED::main.drawStringCentered(valueBuf.data(), slot.start_x,
	                                                   slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
	                                                   kTextSpacingY, slot.width);
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
	const int32_t numDecimalPlaces = getNumDecimalPlaces();
	char buffer[12];
	floatToString(getDisplayValue(), buffer, numDecimalPlaces, numDecimalPlaces);
	strncat(buffer, getUnit(), 4);
	hid::display::OLED::main.drawStringCentred(buffer, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
}

void DecimalWithoutScrolling::drawPixelsForOled() {
	drawDecimal(kTextHugeSpacingX, kTextHugeSizeY, 18);
}

void DecimalWithoutScrolling::drawActualValue(bool justDidHorizontalScroll) {
	const float displayValue = getDisplayValue();
	const int32_t numDecimalPlaces = displayValue > 100 ? 1 : 2;
	char buffer[12];
	floatToString(displayValue, buffer, numDecimalPlaces, numDecimalPlaces);
	display->setText(buffer, true, 3 - numDecimalPlaces);
}
void DecimalWithoutScrolling::configureRenderingOptions(const HorizontalMenuRenderingOptions& options) {
	Decimal::configureRenderingOptions(options);

	const int32_t num_decimal_places = this->getNumDecimalPlaces();
	DEF_STACK_STRING_BUF(notification_value_buf, 10);
	notification_value_buf.appendFloat(this->getDisplayValue(), num_decimal_places, num_decimal_places);
	notification_value_buf.append(getUnit());
	options.notification_value = notification_value_buf.data();
}

} // namespace deluge::gui::menu_item
