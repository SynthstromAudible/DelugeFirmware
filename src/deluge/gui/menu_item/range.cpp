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
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void Range::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.editingColumn = RangeEdit::OFF;

	if (display->have7SEG()) {
		drawValue(0, false);
	}
}

void Range::horizontalEncoderAction(int32_t offset) {

	if (Buttons::isShiftButtonPressed()) {
		return;
	}

	int32_t minCol = 1;
	int32_t maxCol = columnCount();
	int32_t tmpCol = soundEditor.editingColumn;

	offset = offset > 0 ? 1 : -1;

	for (;;) {
		tmpCol = mod(tmpCol + offset, maxCol + 1);

		// Scrolled past all columns, turn off.
		if (tmpCol < minCol || maxCol < tmpCol) {
			cancelEditingIfItsOn();
			return;
		}

		// Can edit this, stop here.
		if (mayEditRangeEdge(tmpCol)) {
			editColumn(tmpCol);
			return;
		}

		// Next column!
	}
}

void Range::editColumn(int32_t col) {
	soundEditor.editingColumn = col;
	drawValueForEditingRange(true);
}

// Returns whether there was anything to cancel
bool Range::cancelEditingIfItsOn() {
	if (soundEditor.editingColumn == 0) {
		return false;
	}

	int32_t startPos = (soundEditor.editingColumn > 1) ? 999 : 0;
	soundEditor.editingColumn = 0;
	drawValue(startPos);
	return true;
}

void Range::drawValue(int32_t startPos, bool renderSidebarToo) {
	if (display->haveOLED()) {

		renderUIsForOled();
	}
	else {
		char* buffer = shortStringBuffer;
		getText(buffer);

		if (strlen(buffer) <= kNumericDisplayLength) {
			display->setText(buffer, true);
		}
		else {
			display->setScrollingText(buffer, startPos);
		}
	}
}

void Range::drawValueForEditingRange(bool blinkImmediately) {
	if (display->haveOLED()) {
		renderUIsForOled();
		return;
	}

	int32_t leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, false);

	int32_t textLength = leftLength + rightLength + 1;

	uint8_t blinkMask[kNumericDisplayLength];
	if (soundEditor.editingColumn == 1) {
		for (int32_t i = 0; i < kNumericDisplayLength; i++) {
			if (i < leftLength + kNumericDisplayLength - std::min(4_i32, textLength))
				blinkMask[i] = 0;
			else
				blinkMask[i] = 255;
		}
	}

	else {
		for (int32_t i = 0; i < kNumericDisplayLength; i++) {
			if (kNumericDisplayLength - 1 - i < rightLength)
				blinkMask[i] = 0;
			else
				blinkMask[i] = 255;
		}
	}

	bool alignRight = (soundEditor.editingColumn > 1) || (textLength < kNumericDisplayLength);

	// Sorta hackish, to reset timing of blinking LED and always show text "on" initially on edit value
	indicator_leds::blinkLed(IndicatorLED::BACK, 255, 0, !blinkImmediately);

	display->setText(buffer, alignRight, 255, true, blinkMask);

	soundEditor.possibleChangeToCurrentRangeDisplay();
}

void Range::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	int32_t leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, soundEditor.editingColumn == RangeEdit::OFF);

	int32_t textLength = leftLength + rightLength + (bool)rightLength;

	int32_t baseY = 18;
	int32_t digitWidth = kTextHugeSpacingX;
	int32_t digitHeight = kTextHugeSizeY;

	int32_t stringWidth = digitWidth * textLength;
	int32_t stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	canvas.drawString(buffer, stringStartX, baseY + OLED_MAIN_TOPMOST_PIXEL, digitWidth, digitHeight);

	int32_t highlightStartX, highlightWidth;

	if (soundEditor.editingColumn == RangeEdit::LEFT) {
		highlightStartX = stringStartX;
		highlightWidth = digitWidth * leftLength;
doHighlightJustOneEdge:
		// Invert the area 1px around the digits being rendered
		baseY += OLED_MAIN_TOPMOST_PIXEL - 1;
		canvas.invertArea(highlightStartX, highlightWidth, baseY, baseY + digitHeight + 1);
	}
	else if (soundEditor.editingColumn == RangeEdit::RIGHT) {
		int32_t stringEndX = (OLED_MAIN_WIDTH_PIXELS + stringWidth) >> 1;
		highlightWidth = digitWidth * rightLength;
		highlightStartX = stringEndX - highlightWidth;
		goto doHighlightJustOneEdge;
	}
}
} // namespace deluge::gui::menu_item
