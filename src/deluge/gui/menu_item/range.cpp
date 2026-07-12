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

	soundEditor.editingRangeEdge = RangeEdit::OFF;
}

void Range::horizontalEncoderAction(int32_t offset) {

	if (Buttons::isShiftButtonPressed()) {
		return;
	}

	// Turn left
	if (offset < 0) {

		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
switchOff:
			soundEditor.editingRangeEdge = RangeEdit::OFF;
			goto justDrawValueForEditingRange;
		}

		else {

			if (mayEditRangeEdge(RangeEdit::LEFT)) {
				soundEditor.editingRangeEdge = RangeEdit::LEFT;
justDrawValueForEditingRange:
				renderUIsForOled();
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

	soundEditor.editingRangeEdge = RangeEdit::OFF;
	renderUIsForOled();
	return true;
}

void Range::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	int32_t leftLength, rightLength;
	char* buffer = shortStringBuffer;

	getText(buffer, &leftLength, &rightLength, soundEditor.editingRangeEdge == RangeEdit::OFF);

	int32_t textLength = leftLength + rightLength + (bool)rightLength;

	int32_t baseY = 18;
	int32_t digitWidth = kTextHugeSpacingX;
	int32_t digitHeight = kTextHugeSizeY;

	int32_t stringWidth = digitWidth * textLength;
	int32_t stringStartX = (OLED_MAIN_WIDTH_PIXELS - stringWidth) >> 1;

	canvas.drawString(buffer, stringStartX, baseY + OLED_MAIN_TOPMOST_PIXEL, digitWidth, digitHeight);

	int32_t highlightStartX, highlightWidth;

	if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
		highlightStartX = stringStartX;
		highlightWidth = digitWidth * leftLength;
doHighlightJustOneEdge:
		// Invert the area 1px around the digits being rendered
		baseY += OLED_MAIN_TOPMOST_PIXEL - 1;
		canvas.invertArea(highlightStartX, highlightWidth, baseY, baseY + digitHeight + 1);
	}
	else if (soundEditor.editingRangeEdge == RangeEdit::RIGHT) {
		int32_t stringEndX = (OLED_MAIN_WIDTH_PIXELS + stringWidth) >> 1;
		highlightWidth = digitWidth * rightLength;
		highlightStartX = stringEndX - highlightWidth;
		goto doHighlightJustOneEdge;
	}
}
} // namespace deluge::gui::menu_item
