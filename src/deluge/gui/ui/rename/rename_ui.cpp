/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include "gui/ui/rename/rename_ui.h"
#include "gui/ui/qwerty_ui.h"
#include "hid/display/oled.h"

RenameUI::RenameUI() {
	scrollPosHorizontal = 0;
	oledShowsUIUnderneath = true;
}

void RenameUI::displayText(bool blinkImmediately) {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	QwertyUI::displayText(blinkImmediately);
}

void RenameUI::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	int32_t windowWidth = 120;
	int32_t windowHeight = 40;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int32_t windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	deluge::hid::display::OLED::clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1, image);

	deluge::hid::display::OLED::drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY, image);

	deluge::hid::display::OLED::drawStringCentred(title, windowMinY + 6, image[0], OLED_MAIN_WIDTH_PIXELS,
	                                              kTextSpacingX, kTextSpacingY);

	int32_t maxNumChars = 17; // "RENAME INSTRUMENT" should be the longest title string, so match that, so match that
	int32_t charsWidthPixels = maxNumChars * kTextSpacingX;
	int32_t charsStartPixel = (OLED_MAIN_WIDTH_PIXELS - charsWidthPixels) >> 1;
	int32_t boxStartPixel = charsStartPixel - 3;

	deluge::hid::display::OLED::drawRectangle(boxStartPixel, 24, OLED_MAIN_WIDTH_PIXELS - boxStartPixel, 38, &image[0]);

	drawTextForOLEDEditing(charsStartPixel, OLED_MAIN_WIDTH_PIXELS - charsStartPixel + 1, 27, maxNumChars, &image[0]);
}
