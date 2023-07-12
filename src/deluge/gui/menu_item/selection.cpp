/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {

void Selection::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	const auto [options, _size] = getOptions();
	numericDriver.setText(options[this->value_]);
#endif
}

#if HAVE_OLED
void Selection::drawPixelsForOled() {
	// Move scroll
	if (soundEditor.menuCurrentScroll > this->value_) {
		soundEditor.menuCurrentScroll = this->value_;
	}
	else if (soundEditor.menuCurrentScroll < this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
		soundEditor.menuCurrentScroll = this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
	}

	char const** options = &getOptions().value[soundEditor.menuCurrentScroll];
	int selectedOption = this->value_ - soundEditor.menuCurrentScroll;

	drawItemsForOled(options, selectedOption);
}
#endif
} // namespace deluge::gui::menu_item
