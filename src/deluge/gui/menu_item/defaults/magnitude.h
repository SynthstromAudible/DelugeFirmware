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
#include "gui/menu_item/enumeration.h"
#include "storage/flash_storage.h"
#include "gui/menu_item/toggle.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::defaults {
class Magnitude final : public Enumeration {
public:
	using Enumeration::Enumeration;
	size_t size() override { return 7; }
	void readCurrentValue() override { this->value_ = FlashStorage::defaultMagnitude; }
	void writeCurrentValue() override { FlashStorage::defaultMagnitude = this->value_; }
#if HAVE_OLED
	void drawPixelsForOled() override {
		char buffer[12];
		intToString(96 << this->value_, buffer);
		OLED::drawStringCentred(buffer, 20 + OLED_MAIN_TOPMOST_PIXEL, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                        18, 20);
	}
#else
	void drawValue() override {
		numericDriver.setTextAsNumber(96 << this->value_);
	}
#endif
};
} // namespace deluge::gui::menu_item::defaults
