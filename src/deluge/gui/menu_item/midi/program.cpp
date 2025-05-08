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
		// drawActualValue(true); // TODO
	}
}

void Program::drawValueAtPos(int32_t v, int32_t x) {
	int32_t baseY = OLED_MAIN_TOPMOST_PIXEL + 15 + kTextSpacingY;
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
	int32_t baseY = OLED_MAIN_TOPMOST_PIXEL + 15;
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_BANK), baseY, kTextSpacingX * 5, kTextSpacingY, 21);
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_SUB_BANK), baseY, kTextSpacingX * 8, kTextSpacingY, 64);
	canvas.drawStringCentred(l10n::get(l10n::String::STRING_FOR_PGM), baseY, kTextSpacingX * 5, kTextSpacingY, 107);

	this->drawValueAtPos(this->getBank(), 21);
	this->drawValueAtPos(this->getSub(), 64);
	this->drawValueAtPos(this->getPgm(), 107);

	int32_t cursorX = 21 + (cursorPos * 43);
	hid::display::OLED::setupBlink(cursorX - 20, 40, 41, 42, movingCursor);
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

void Program::selectEncoderAction(int32_t offset) {
	switch (cursorPos) {
	case 0:
		this->setBank(wrapValue(this->getBank() + offset));
		break;
	case 1:
		this->setSub(wrapValue(this->getSub() + offset));
		break;
	case 2:
		this->setPgm(wrapValue(this->getPgm() + offset));
		break;
	default:
		return;
	}

	writeCurrentValue();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue(); // Probably not necessary either...
	}
}

} // namespace deluge::gui::menu_item::midi
