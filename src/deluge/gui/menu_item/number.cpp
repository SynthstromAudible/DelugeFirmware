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

#include "number.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item {

void Number::drawBar(int32_t yTop, int32_t marginL, int32_t marginR) {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	if (marginR == -1) {
		marginR = marginL;
	}
	int32_t height = 7;

	int32_t leftMost = marginL;
	int32_t rightMost = OLED_MAIN_WIDTH_PIXELS - marginR - 1;

	int32_t y = OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_VISIBLE_HEIGHT * 0.78;

	int32_t endLineHalfHeight = 8;

	/*
	deluge::hid::display::OLED::drawHorizontalLine(y, leftMost, rightMost, deluge::hid::display::OLED::oledMainImage);
	deluge::hid::display::OLED::drawVerticalLine(leftMost, y, y + endLineHalfHeight,
	deluge::hid::display::OLED::oledMainImage); deluge::hid::display::OLED::drawVerticalLine(rightMost, y, y +
	endLineHalfHeight, deluge::hid::display::OLED::oledMainImage);
*/
	int32_t minValue = getMinValue();
	int32_t maxValue = getMaxValue();
	uint32_t range = maxValue - minValue;
	float posFractional = (float)(this->getValue() - minValue) / range;
	float zeroPosFractional = (float)(-minValue) / range;

	int32_t width = rightMost - leftMost;
	int32_t posHorizontal = (int32_t)(posFractional * width + 0.5);
	int32_t zeroPosHorizontal = (int32_t)(zeroPosFractional * width);

	if (posHorizontal <= zeroPosHorizontal) {
		int32_t xMin = leftMost + posHorizontal;
		canvas.invertArea(xMin, zeroPosHorizontal - posHorizontal + 1, yTop, yTop + height);
	}
	else {
		int32_t xMin = leftMost + zeroPosHorizontal;
		canvas.invertArea(xMin, posHorizontal - zeroPosHorizontal, yTop, yTop + height);
	}
	canvas.drawRectangle(leftMost, yTop, rightMost, yTop + height);
}

void Number::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
	renderColumnLabel(startX, width, startY);

	DEF_STACK_STRING_BUF(paramValue, 10);
	paramValue.appendInt(getValue());

	image.drawStringCentered(paramValue, startX, startY + kTextSpacingY + 4, kTextTitleSpacingX, kTextTitleSizeY,
	                         width);
}

void Number::selectEncoderAction(int32_t offset) {
	Value::selectEncoderAction(offset);

	DEF_STACK_STRING_BUF(popupBuf, 15);
	popupBuf.append(getName());
	popupBuf.append(": ");
	popupBuf.appendInt(getValue());
	display->displayPopup(popupBuf.c_str(), 3, false, 255U, 1, PopupType::TOP_LEFT);
}

} // namespace deluge::gui::menu_item
