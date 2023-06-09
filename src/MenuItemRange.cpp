/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include <MenuItemRange.h>

#include "soundeditor.h"
#include "numericdriver.h"
#include "functions.h"
#include "IndicatorLEDs.h"
#include "matrixdriver.h"
#include "Buttons.h"
#include "oled.h"

void MenuItemRange::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.editingRangeEdge = RANGE_EDIT_OFF;

#if !HAVE_OLED
	drawValue(0, false);
#endif
}

void MenuItemRange::horizontalEncoderAction(int offset) {

	if (Buttons::isShiftButtonPressed()) return;

	// Turn left
	if (offset < 0) {

		if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {
switchOff:
			soundEditor.editingRangeEdge = RANGE_EDIT_OFF;
#if HAVE_OLED
			goto justDrawValueForEditingRange;
#else
			int startPos = (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) ? 999 : 0;
			drawValue(startPos);
#endif
		}

		else {

			if (mayEditRangeEdge(RANGE_EDIT_LEFT)) {
				soundEditor.editingRangeEdge = RANGE_EDIT_LEFT;
justDrawValueForEditingRange:
#if HAVE_OLED
				renderUIsForOled();
#else
				drawValueForEditingRange(true);
#endif
			}
			else {
				if (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) {
					goto switchOff;
				}
			}
		}
	}

	// Turn right
	else {
		if (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) {
			goto switchOff;
		}

		else {

			if (mayEditRangeEdge(RANGE_EDIT_RIGHT)) {
				soundEditor.editingRangeEdge = RANGE_EDIT_RIGHT;
				goto justDrawValueForEditingRange;
			}
			else {
				if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {
					goto switchOff;
				}
			}
		}
	}
}

// Returns whether there was anything to cancel
bool MenuItemRange::cancelEditingIfItsOn() {
	if (!soundEditor.editingRangeEdge) return false;

	int startPos = (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) ? 999 : 0;
	soundEditor.editingRangeEdge = RANGE_EDIT_OFF;
	drawValue(startPos);
	return true;
}

void MenuItemRange::drawValue(int startPos, bool renderSidebarToo) {
#if HAVE_OLED
	renderUIsForOled();
#else
	char* buffer = shortStringBuffer;
	getText(buffer);

	if (strlen(buffer) <= NUMERIC_DISPLAY_LENGTH) {
		numericDriver.setText(buffer, true);
	}
	else {
		numericDriver.setScrollingText(buffer, startPos);
	}
#endif
}

void MenuItemRange::drawValueForEditingRange(bool blinkImmediately) {
#if HAVE_OLED
	renderUIsForOled();
#else
	int leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, false);

	int textLength = leftLength + rightLength + 1;

	uint8_t blinkMask[NUMERIC_DISPLAY_LENGTH];
	if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {
		for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
			if (i < leftLength + NUMERIC_DISPLAY_LENGTH - getMin(4, textLength)) blinkMask[i] = 0;
			else blinkMask[i] = 255;
		}
	}

	else {
		for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
			if (NUMERIC_DISPLAY_LENGTH - 1 - i < rightLength) blinkMask[i] = 0;
			else blinkMask[i] = 255;
		}
	}

	bool alignRight = (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) || (textLength < NUMERIC_DISPLAY_LENGTH);

	IndicatorLEDs::blinkLed(
	    backLedX, backLedY, 255, 0,
	    !blinkImmediately); // Sorta hackish, to reset timing of blinking LED and always show text "on" initially on edit value
	numericDriver.setText(buffer, alignRight, 255, true, blinkMask);

	soundEditor.possibleChangeToCurrentRangeDisplay();
#endif
}

#if HAVE_OLED
void MenuItemRange::drawPixelsForOled() {
	int leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, !soundEditor.editingRangeEdge);

	int textLength = leftLength + rightLength + (bool)rightLength;

	int baseY = 18;
	int digitWidth = TEXT_HUGE_SPACING_X;
	int digitHeight = TEXT_HUGE_SIZE_Y;

	int stringWidth = digitWidth * textLength;
	int stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	OLED::drawString(buffer, stringStartX, baseY, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, digitWidth,
	                 digitHeight);

	int hilightStartX, hilightWidth;

	if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {
		hilightStartX = stringStartX;
		hilightWidth = digitWidth * leftLength;
doHilightJustOneEdge:
		OLED::invertArea(hilightStartX, hilightWidth, baseY - 1, baseY + digitHeight + 1, OLED::oledMainImage);
	}
	else if (soundEditor.editingRangeEdge == RANGE_EDIT_RIGHT) {
		int stringEndX = (OLED_MAIN_WIDTH_PIXELS + stringWidth) >> 1;
		hilightWidth = digitWidth * rightLength;
		hilightStartX = stringEndX - hilightWidth;
		goto doHilightJustOneEdge;
	}
}
#endif
