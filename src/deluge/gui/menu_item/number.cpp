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
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"

#if HAVE_OLED
#include "hid/display/oled.h"
#endif

namespace menu_item {

#if HAVE_OLED
void Number::drawBar(int yTop, int marginL, int marginR) {
	if (marginR == -1) {
		marginR = marginL;
	}
	int height = 7;

	int leftMost = marginL;
	int rightMost = OLED_MAIN_WIDTH_PIXELS - marginR - 1;

	int y = OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_VISIBLE_HEIGHT * 0.78;

	int endLineHalfHeight = 8;

	/*
	OLED::drawHorizontalLine(y, leftMost, rightMost, OLED::oledMainImage);
	OLED::drawVerticalLine(leftMost, y, y + endLineHalfHeight, OLED::oledMainImage);
	OLED::drawVerticalLine(rightMost, y, y + endLineHalfHeight, OLED::oledMainImage);
*/
	int minValue = getMinValue();
	int maxValue = getMaxValue();
	unsigned int range = maxValue - minValue;
	float posFractional = (float)(soundEditor.currentValue - minValue) / range;
	float zeroPosFractional = (float)(-minValue) / range;

	int width = rightMost - leftMost;
	int posHorizontal = (int)(posFractional * width + 0.5);
	int zeroPosHorizontal = (int)(zeroPosFractional * width);

	if (posHorizontal <= zeroPosHorizontal) {
		int xMin = leftMost + posHorizontal;
		OLED::invertArea(xMin, zeroPosHorizontal - posHorizontal + 1, yTop, yTop + height, OLED::oledMainImage);
	}
	else {
		int xMin = leftMost + zeroPosHorizontal;
		OLED::invertArea(xMin, posHorizontal - zeroPosHorizontal, yTop, yTop + height, OLED::oledMainImage);
	}
	OLED::drawRectangle(leftMost, yTop, rightMost, yTop + height, OLED::oledMainImage);
}
#endif

} // namespace menu_item
