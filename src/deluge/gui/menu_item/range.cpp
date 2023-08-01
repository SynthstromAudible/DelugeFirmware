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

#include "range.h"

#include "gui/menu_item/range.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"
#include "util/functions.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "hid/buttons.h"

namespace menu_item {

void Range::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.editingRangeEdge = RangeEdit::OFF;

	if (display.type != DisplayType::OLED) {
		drawValue(0, false);
	}
}

void Range::horizontalEncoderAction(int offset) {

	if (Buttons::isShiftButtonPressed()) {
		return;
	}

	// Turn left
	if (offset < 0) {

		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
switchOff:
			soundEditor.editingRangeEdge = RangeEdit::OFF;
			if (display.type == DisplayType::OLED) {
				goto justDrawValueForEditingRange;
			}
			else {
				int startPos = (soundEditor.editingRangeEdge == RangeEdit::RIGHT) ? 999 : 0;
				drawValue(startPos);
			}
		}

		else {

			if (mayEditRangeEdge(RangeEdit::LEFT)) {
				soundEditor.editingRangeEdge = RangeEdit::LEFT;
justDrawValueForEditingRange:
				if (display.type == DisplayType::OLED) {
					renderUIsForOled();
				}
				else {
					drawValueForEditingRange(true);
				}
			}
			else {
				if (soundEditor.editingRangeEdge == RangeEdit::RIGHT) {
					goto switchOff;
				}
			}
		}
	}

	// Turn right
	else {
		if (soundEditor.editingRangeEdge == RangeEdit::RIGHT) {
			goto switchOff;
		}

		else {

			if (mayEditRangeEdge(RangeEdit::RIGHT)) {
				soundEditor.editingRangeEdge = RangeEdit::RIGHT;
				goto justDrawValueForEditingRange;
			}
			else {
				if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
					goto switchOff;
				}
			}
		}
	}
}

// Returns whether there was anything to cancel
bool Range::cancelEditingIfItsOn() {
	if (soundEditor.editingRangeEdge == RangeEdit::OFF) {
		return false;
	}

	int startPos = (soundEditor.editingRangeEdge == RangeEdit::RIGHT) ? 999 : 0;
	soundEditor.editingRangeEdge = RangeEdit::OFF;
	drawValue(startPos);
	return true;
}

void Range::drawValue(int startPos, bool renderSidebarToo) {
	if (display.type == DisplayType::OLED) {

		renderUIsForOled();
	}
	else {
		char* buffer = shortStringBuffer;
		getText(buffer);

		if (strlen(buffer) <= kNumericDisplayLength) {
			display.setText(buffer, true);
		}
		else {
			display.setScrollingText(buffer, startPos);
		}
	}
}

void Range::drawValueForEditingRange(bool blinkImmediately) {
	if (display.type == DisplayType::OLED) {
		renderUIsForOled();
		return;
	}

	int leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, false);

	int textLength = leftLength + rightLength + 1;

	uint8_t blinkMask[kNumericDisplayLength];
	if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
		for (int i = 0; i < kNumericDisplayLength; i++) {
			if (i < leftLength + kNumericDisplayLength - getMin(4, textLength))
				blinkMask[i] = 0;
			else
				blinkMask[i] = 255;
		}
	}

	else {
		for (int i = 0; i < kNumericDisplayLength; i++) {
			if (kNumericDisplayLength - 1 - i < rightLength)
				blinkMask[i] = 0;
			else
				blinkMask[i] = 255;
		}
	}

	bool alignRight = (soundEditor.editingRangeEdge == RangeEdit::RIGHT) || (textLength < kNumericDisplayLength);

	// Sorta hackish, to reset timing of blinking LED and always show text "on" initially on edit value
	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !blinkImmediately);

	display.setText(buffer, alignRight, 255, true, blinkMask);

	soundEditor.possibleChangeToCurrentRangeDisplay();
}

void Range::drawPixelsForOled() {
	int leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, soundEditor.editingRangeEdge == RangeEdit::OFF);

	int textLength = leftLength + rightLength + (bool)rightLength;

	int baseY = 18;
	int digitWidth = kTextHugeSpacingX;
	int digitHeight = kTextHugeSizeY;

	int stringWidth = digitWidth * textLength;
	int stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	OLED::drawString(buffer, stringStartX, baseY, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, digitWidth,
	                 digitHeight);

	int hilightStartX, hilightWidth;

	if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
		hilightStartX = stringStartX;
		hilightWidth = digitWidth * leftLength;
doHilightJustOneEdge:
		OLED::invertArea(hilightStartX, hilightWidth, baseY - 1, baseY + digitHeight + 1, OLED::oledMainImage);
	}
	else if (soundEditor.editingRangeEdge == RangeEdit::RIGHT) {
		int stringEndX = (OLED_MAIN_WIDTH_PIXELS + stringWidth) >> 1;
		hilightWidth = digitWidth * rightLength;
		hilightStartX = stringEndX - hilightWidth;
		goto doHilightJustOneEdge;
	}
}
} // namespace menu_item
