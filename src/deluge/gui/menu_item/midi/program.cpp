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

#include "program.h"
#include "gui/menu_item/midi/program.h"

#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "util/functions.h"

namespace deluge::gui::menu_item::midi {

bool movingCursor = false; // Sorry, ugly hack.

void Program::horizontalEncoderAction(int32_t offset) {

	if (Buttons::isShiftButtonPressed()) {
		return;
	}

	if (offset < 0) {
		if (cursorPos > 0) {
			cursorPos--;
		}
	}
	else {
		if (cursorPos < 2) {
			cursorPos++;
		}
	}

	if (display->haveOLED()) {
		movingCursor = true;
		renderUIsForOled();
		movingCursor = false;
	}
	else {
		drawActualValue(true);
	}
}

void Program::drawValueAtPos(int32_t v, int32_t x) {
	int32_t baseY = OLED_MAIN_TOPMOST_PIXEL + 15 + kTextSpacingY + 4;
	char buffer[8];
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	if (v == 128) {
		canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_NONE), baseY, kTextSpacingX * 5, kTextSpacingY, x);
	}
	else {
		intToString(v + 1, buffer, 1);
		canvas.drawStringCentred(buffer, baseY, kTextTitleSpacingX * 5, kTextTitleSizeY, x);
	}
}

void Program::drawPixelsForOled() {
	int32_t baseY = OLED_MAIN_TOPMOST_PIXEL + 14;
	int32_t w;
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_BANK), baseY, kTextSpacingX * 5, kTextSpacingY, 21);
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_SUB_BANK), baseY, kTextSpacingX * 8, kTextSpacingY, 64);
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_PGM), baseY, kTextSpacingX * 5, kTextSpacingY, 107);

	this->drawValueAtPos(this->getBank(), 21);
	this->drawValueAtPos(this->getSub(), 64);
	this->drawValueAtPos(this->getPgm(), 107);

	baseY += kTextSpacingY;
	w = 2 * kTextSpacingX;
	canvas.drawHorizontalLine(baseY, 20 - w, 20 + w);
	w = 4 * kTextSpacingX;
	canvas.drawHorizontalLine(baseY, 63 - w, 64 + w);
	w = 3 * kTextSpacingX / 2;
	canvas.drawHorizontalLine(baseY, 106 - w, 106 + w);

	int32_t cursorX = 21 + (cursorPos * 43);
	hid::display::OLED::setupBlink(cursorX - kTextSpacingX * 2, kTextSpacingX * 4, 45, 47, movingCursor);
}

int32_t wrapValue(int32_t value) {
	if (value >= 129) {
		value -= 129;
	}
	else if (value < 0) {
		value += 129;
	}
	return value;
}

int32_t Program::getCursorValue() {
	switch (cursorPos) {
	case 0:
		return this->getBank();
	case 1:
		return this->getSub();
	case 2:
		return this->getPgm();
	default:
		return 128;
	}
}

void Program::setCursorValue(int32_t value) {
	switch (cursorPos) {
	case 0:
		this->setBank(value);
		break;
	case 1:
		this->setSub(value);
		break;
	case 2:
		this->setPgm(value);
		break;
	default:
		return;
	}
}

void Program::selectEncoderAction(int32_t offset) {
	this->setCursorValue(wrapValue(this->getCursorValue() + offset));

	writeCurrentValue();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

void Program::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawActualValue();
	}
}

void Program::drawActualValue(bool justDidHorizontalScroll) {
	int32_t stringLength = 0;
	char buffer[12];

	int32_t v = this->getCursorValue();
	if (v == 128) {
		if (cursorPos == 0) {
			// strcpy(buffer, l10n::get(l10n::String::STRING_FOR_NONE));
			strcpy(buffer, "BANK.");
		}
		else if (cursorPos == 1) {
			strcpy(buffer, ".SUB.");
		}
		else {
			strcpy(buffer, ".PGM");
		}
	}
	else if (cursorPos == 0) {
		buffer[0] = 'b';
		intToString(v + 1, buffer + 1, 3);
		buffer[4] = '.';
	}
	else if (cursorPos == 1) {
		buffer[0] = 's';
		buffer[1] = '.';
		intToString(v + 1, buffer + 2, 3);
		buffer[5] = '.';
		buffer[6] = '\0';
	}
	else if (cursorPos == 2) {
		buffer[0] = 'p';
		buffer[1] = '.';
		intToString(v + 1, buffer + 2, 3);
		buffer[5] = '\0';
	}
	for (int i = 1; i < strlen(buffer); i++) {
		if (buffer[i] == '.')
			continue;
		if (buffer[i] == '0')
			buffer[i] = ' ';
		else
			break;
	}
	display->setText(buffer);
}

} // namespace deluge::gui::menu_item::midi
