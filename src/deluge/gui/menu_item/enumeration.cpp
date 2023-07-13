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

#include "enumeration.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"

extern "C" {
#if HAVE_OLED
#include "RZA1/oled/oled_low_level.h"
#endif
}

namespace deluge::gui::menu_item {

void Enumeration::beginSession(MenuItem* navigatedBackwardFrom) {
	Value::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	soundEditor.menuCurrentScroll = 0;
#else
	drawValue();
#endif
}

void Enumeration::selectEncoderAction(int offset) {
	this->value_ += offset;
	int numOptions = size();

#if HAVE_OLED
	if (this->value_ >= numOptions) {
		this->value_ = numOptions;
	}
	else if (this->value_ < 0) {
		this->value_ = 0;
	}
#else
	if (this->value_ >= numOptions) {
		this->value_ -= numOptions;
	}
	else if (this->value_ < 0) {
		this->value_ += numOptions;
	}
#endif

	Value::selectEncoderAction(offset);
}

void Enumeration::drawValue() {
#if HAVE_OLED
	renderUIsForOled();
#else
	numericDriver.setTextAsNumber(this->value_);
#endif
}
} // namespace deluge::gui::menu_item
