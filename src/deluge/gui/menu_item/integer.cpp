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
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/cfunctions.h"

namespace deluge::gui::menu_item {

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

void Integer::drawValue() {
	display->setTextAsNumber(getDisplayValue());
}

void IntegerWithOff::drawValue() {
	if (this->getValue() == 0) {
		display->setText(l10n::get(l10n::String::STRING_FOR_DISABLED));
	}
	else {
		Integer::drawValue();
	}
}
void IntegerWithOff::drawPixelsForOled() {
	if (this->getValue() == 0) {
		deluge::hid::display::OLED::main.drawStringCentred("OFF", 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	else {
		Integer::drawPixelsForOled();
	}
}

void Integer::drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
	char buffer[12];
	intToString(getDisplayValue(), buffer, 1);
	strncat(buffer, getUnit(), 4);
	deluge::hid::display::OLED::main.drawStringCentred(buffer, yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
}

void Integer::drawPixelsForOled() {
	drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18);
}

// TODO: This is identical with the patched integer version. Can we share the code? How about
// having a Integer class which both PatchedInteger and UnpatchedInteger would inherit from?
void Integer::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
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
	float value = getValue();
	if (isDisabledBelowZero() && value < 0) {
		paramValue.append("OFF");
	}
	else {
		paramValue.appendInt(this->getValue());
	}
	int32_t pxLen = image.getStringWidthInPixels(paramValue.c_str(), kTextTitleSizeY);
	int32_t pad = (width + 1 - pxLen) / 2;
	image.drawString(paramValue.c_str(), startX + pad, startY + kTextSpacingY + 2, kTextTitleSpacingX, kTextTitleSizeY,
	                 0, startX + width);
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
