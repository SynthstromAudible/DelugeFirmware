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
#include "hid/display/oled.h"

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
void drawListItemsForOled(Sized<char const**> options_sized, const int value, const int scroll) {
	auto [options, size] = options_sized;

	const void *begin = &options[scroll]; // fast-forward to the first option visible
	const void *end = &options[size];

	const int selectedOption = value - scroll;

	int baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
	baseY += OLED_MAIN_TOPMOST_PIXEL;

	for (int o = 0; o < OLED_HEIGHT_CHARS - 1; o++) {
		if (&options[o] == end) {
			break;
		}

		int yPixel = o * TEXT_SPACING_Y + baseY;

		OLED::drawString(options[o], TEXT_SPACING_X, yPixel, OLED::oledMainImage[0], OLED_MAIN_WIDTH_PIXELS,
		                 TEXT_SPACING_X, TEXT_SPACING_Y);

		if (o == selectedOption) {
			OLED::invertArea(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8, &OLED::oledMainImage[0]);
			OLED::setupSideScroller(0, options[o], TEXT_SPACING_X, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8,
			                        TEXT_SPACING_X, TEXT_SPACING_Y, true);
		}
	}
}

void Selection::drawPixelsForOled() {
	// Move scroll
	if (soundEditor.menuCurrentScroll > this->value_) {
		soundEditor.menuCurrentScroll = this->value_;
	}
	else if (soundEditor.menuCurrentScroll < this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
		soundEditor.menuCurrentScroll = this->value_ - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
	}

	drawListItemsForOled(getOptions(), this->value_, soundEditor.menuCurrentScroll);
}
#endif
} // namespace deluge::gui::menu_item
