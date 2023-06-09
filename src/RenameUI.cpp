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

#include <RenameUI.h>

#if HAVE_OLED
#include "oled.h"
#endif

RenameUI::RenameUI() {
	scrollPosHorizontal = 0;
#if HAVE_OLED
	oledShowsUIUnderneath = true;
#endif
}

#if HAVE_OLED

void RenameUI::displayText(bool blinkImmediately) {
	renderUIsForOled();
}

void RenameUI::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	int windowWidth = 100;
	int windowHeight = 40;

	int windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	OLED::clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1, image);

	OLED::drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY, image);

	OLED::drawStringCentred(title, windowMinY + 6, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_SPACING_X, TEXT_SPACING_Y);

	int maxNumChars = 12;
	int charsWidthPixels = maxNumChars * TEXT_SPACING_X;
	int charsStartPixel = (OLED_MAIN_WIDTH_PIXELS - charsWidthPixels) >> 1;
	int boxStartPixel = charsStartPixel - 3;

	OLED::drawRectangle(boxStartPixel, 24, OLED_MAIN_WIDTH_PIXELS - boxStartPixel, 38, &image[0]);

	drawTextForOLEDEditing(charsStartPixel, OLED_MAIN_WIDTH_PIXELS - charsStartPixel + 1, 27, maxNumChars, &image[0]);
}

#endif
