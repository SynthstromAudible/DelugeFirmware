/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::arpeggiator {
class Ratchets : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->numRatchets); }
	void writeCurrentValue() override { soundEditor.currentArpSettings->numRatchets = this->getValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 3; }
	bool isRelevant(Sound* sound, int32_t whichThing) override { return !soundEditor.editingKit(); }

	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) {
		if (this->getValue() == 0) {
			deluge::hid::display::OLED::drawStringCentred(
			    l10n::get(l10n::String::STRING_FOR_OFF), yPixel + OLED_MAIN_TOPMOST_PIXEL,
			    deluge::hid::display::OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS, textWidth, textHeight);
		}
		else {
			char buffer[12];
			intToString(1 << this->getValue(), buffer, 1);
			deluge::hid::display::OLED::drawStringCentred(buffer, yPixel + OLED_MAIN_TOPMOST_PIXEL,
			                                              deluge::hid::display::OLED::oledMainImage[0],
			                                              OLED_MAIN_WIDTH_PIXELS, textWidth, textHeight);
		}
	}

	void drawValue() {
		if (this->getValue() == 0) {
			display->setText(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		else {
			display->setTextAsNumber(1 << this->getValue());
		}
	}
};
} // namespace deluge::gui::menu_item::arpeggiator
