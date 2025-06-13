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
#include "extern.h"
#include "gui/ui/qwerty_ui.h"
#include "hid/display/oled.h"

RenameUI::RenameUI(const char* title_) : QwertyUI() {
	title = title_;
	scrollPosHorizontal = 0;
	oledShowsUIUnderneath = true;
}

bool RenameUI::opened() {
	if (!QwertyUI::opened()) {
		return false;
	}
	if (!canRename()) {
		return false;
	}

	String name = getName();
	enteredText.set(&name);

	displayText();
	drawKeys();

	return true;
}

void RenameUI::enterKeyPress() {
	if (enteredText.isEmpty() && !allowEmpty()) {
		return;
	}
	if (trySetName(&enteredText)) {
		exitUI();
	}
}

void RenameUI::displayText(bool blinkImmediately) {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		QwertyUI::displayText(blinkImmediately);
	}
}

bool RenameUI::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0b11;
	return true;
}

void RenameUI::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {

	int32_t windowWidth = 120;
	int32_t windowHeight = 40;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	int32_t windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	int32_t windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

	canvas.clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);
	canvas.drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	canvas.drawStringCentred(title, windowMinY + 6, kTextSpacingX, kTextSpacingY);

	int32_t maxNumChars = 17; // "RENAME INSTRUMENT" should be the longest title string, so match that, so match that
	int32_t charsWidthPixels = maxNumChars * kTextSpacingX;
	int32_t charsStartPixel = (OLED_MAIN_WIDTH_PIXELS - charsWidthPixels) >> 1;
	int32_t boxStartPixel = charsStartPixel - 3;

	canvas.drawRectangle(boxStartPixel, 24, OLED_MAIN_WIDTH_PIXELS - boxStartPixel, 38);

	drawTextForOLEDEditing(charsStartPixel, OLED_MAIN_WIDTH_PIXELS - charsStartPixel + 1, 27, maxNumChars, canvas);
}

bool RenameUI::exitUI() {
	display->setNextTransitionDirection(-1);
	close();
	return true;
}

ActionResult RenameUI::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	// Back button
	if (b == BACK) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			exitUI();
		}
	}

	// Select encoder button
	else if (b == SELECT_ENC) {
		if (on && !currentUIMode) {
			if (inCardRoutine) {
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}
			enterKeyPress();
		}
	}

	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

ActionResult RenameUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Main pad
	if (x < kDisplayWidth) {
		return QwertyUI::padAction(x, y, on);
	}

	// Otherwise, exit
	if (on && !currentUIMode) {
		if (sdRoutineLock) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		exitUI();
	}

	return ActionResult::DEALT_WITH;
}

ActionResult RenameUI::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	return ActionResult::DEALT_WITH;
}
